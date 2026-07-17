#include "search.h"

#include <time.h>

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

#define CHECK_MOVE_SCORE 5000
#define KILLER_MOVE_SCORE 4000
#define MAX_KILLER_PLY 64

typedef struct SearchContext {
    clock_t start_time;
    int time_limit_ms;
    int stopped;
    Move killer_moves[MAX_KILLER_PLY][2];
} SearchContext;

static void clear_move(Move *move) {
    move->from = NO_SQUARE;
    move->to = NO_SQUARE;
    move->promotion = PIECE_NONE;
    move->flags = MOVE_FLAG_NONE;
}

static void initialize_search_context(
    SearchContext *context,
    int time_limit_ms
) {
    int ply;

    context->start_time = clock();
    context->time_limit_ms = time_limit_ms;
    context->stopped = 0;

    for (ply = 0; ply < MAX_KILLER_PLY; ++ply) {
        clear_move(&context->killer_moves[ply][0]);
        clear_move(&context->killer_moves[ply][1]);
    }
}

static int moves_are_equal(Move first, Move second) {
    return first.from == second.from &&
           first.to == second.to &&
           first.promotion == second.promotion &&
           first.flags == second.flags;
}

static int killer_move_score(
    const SearchContext *context,
    int ply,
    Move move
) {
    if (context == 0 || ply < 0 || ply >= MAX_KILLER_PLY) {
        return 0;
    }

    if (moves_are_equal(move, context->killer_moves[ply][0])) {
        return KILLER_MOVE_SCORE;
    }

    if (moves_are_equal(move, context->killer_moves[ply][1])) {
        return KILLER_MOVE_SCORE - 1;
    }

    return 0;
}

static void record_killer_move(SearchContext *context, int ply, Move move) {
    if (context == 0 || ply < 0 || ply >= MAX_KILLER_PLY ||
        (move.flags & MOVE_FLAG_CAPTURE) != 0 ||
        moves_are_equal(move, context->killer_moves[ply][0])) {
        return;
    }

    context->killer_moves[ply][1] = context->killer_moves[ply][0];
    context->killer_moves[ply][0] = move;
}

static int search_has_stopped(SearchContext *context) {
    clock_t elapsed;

    if (context == 0 || context->time_limit_ms <= 0) {
        return 0;
    }

    if (context->stopped) {
        return 1;
    }

    elapsed = clock() - context->start_time;

    if (elapsed * 1000 / CLOCKS_PER_SEC >= context->time_limit_ms) {
        context->stopped = 1;
        return 1;
    }

    return 0;
}

static int piece_value(Piece piece) {
    switch (piece_type(piece)) {
        case PIECE_TYPE_PAWN:
            return PAWN_VALUE;
        case PIECE_TYPE_KNIGHT:
            return KNIGHT_VALUE;
        case PIECE_TYPE_BISHOP:
            return BISHOP_VALUE;
        case PIECE_TYPE_ROOK:
            return ROOK_VALUE;
        case PIECE_TYPE_QUEEN:
            return QUEEN_VALUE;
        case PIECE_TYPE_KING:
            return KING_VALUE;
        case PIECE_TYPE_NONE:
            return 0;
    }

    return 0;
}

static int position_is_in_check(const Position *position);

static int move_gives_check(Position *position, Move move) {
    UndoState undo;
    int gives_check;

    if (!make_move(position, move, &undo)) {
        return 0;
    }

    gives_check = position_is_in_check(position);
    undo_move(position, move, &undo);
    return gives_check;
}

static int move_order_score(
    Position *position,
    Move move,
    int order_checks,
    const SearchContext *context,
    int ply
) {
    Piece attacker;
    Piece victim;

    if ((move.flags & MOVE_FLAG_CAPTURE) == 0) {
        if (order_checks && move_gives_check(position, move)) {
            return CHECK_MOVE_SCORE;
        }

        return killer_move_score(context, ply, move);
    }

    attacker = position_piece_at(position, move.from);

    if (move.flags & MOVE_FLAG_EN_PASSANT) {
        victim = PIECE_WHITE_PAWN;
    } else {
        victim = position_piece_at(position, move.to);
    }

    return 10000 + piece_value(victim) * 16 - piece_value(attacker);
}

static void order_moves(
    Position *position,
    MoveList *moves,
    int order_checks,
    const SearchContext *context,
    int ply
) {
    int index;
    int scores[MAX_MOVES];

    if (position == 0 || moves == 0) {
        return;
    }

    for (index = 0; index < moves->count; ++index) {
        scores[index] = move_order_score(
            position,
            moves->moves[index],
            order_checks,
            context,
            ply
        );
    }

    for (index = 0; index < moves->count; ++index) {
        int best_index = index;
        int next;

        for (next = index + 1; next < moves->count; ++next) {
            if (scores[next] > scores[best_index]) {
                best_index = next;
            }
        }

        if (best_index != index) {
            Move move = moves->moves[index];
            int score = scores[index];

            moves->moves[index] = moves->moves[best_index];
            moves->moves[best_index] = move;
            scores[index] = scores[best_index];
            scores[best_index] = score;
        }
    }
}

static int position_is_in_check(const Position *position) {
    int king_square = find_king(position, position->side_to_move);

    return is_valid_square(king_square) &&
           is_square_attacked(
               position,
               king_square,
               opposite_color(position->side_to_move)
           );
}

static void clear_variation(PrincipalVariation *variation) {
    if (variation != 0) {
        variation->count = 0;
    }
}

static void update_variation(
    PrincipalVariation *variation,
    Move move,
    const PrincipalVariation *child_variation
) {
    int index;

    if (variation == 0) {
        return;
    }

    variation->moves[0] = move;
    variation->count = 1;

    if (child_variation == 0) {
        return;
    }

    for (index = 0;
         index < child_variation->count &&
         variation->count < MAX_PRINCIPAL_VARIATION;
         ++index) {
        variation->moves[variation->count] = child_variation->moves[index];
        variation->count++;
    }
}

static int quiescence(
    Position *position,
    int alpha,
    int beta,
    int ply,
    SearchContext *context
) {
    MoveList moves;
    int in_check;
    int index;

    if (search_has_stopped(context)) {
        return 0;
    }

    generate_legal_moves(position, &moves);
    in_check = position_is_in_check(position);

    if (moves.count == 0) {
        if (in_check) {
            return -SEARCH_CHECKMATE + ply;
        }

        return 0;
    }

    if (!in_check) {
        int stand_pat = evaluate_position(position);

        if (stand_pat >= beta) {
            return beta;
        }

        if (stand_pat > alpha) {
            alpha = stand_pat;
        }
    }

    order_moves(position, &moves, 0, context, ply);

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        UndoState undo;
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        if (!in_check && (move.flags & MOVE_FLAG_CAPTURE) == 0) {
            continue;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        score = -quiescence(position, -beta, -alpha, ply + 1, context);
        undo_move(position, move, &undo);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (score >= beta) {
            return beta;
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

static int negamax(
    Position *position,
    int depth,
    int alpha,
    int beta,
    int ply,
    PrincipalVariation *variation,
    SearchContext *context
) {
    MoveList moves;
    int index;

    clear_variation(variation);

    if (search_has_stopped(context)) {
        return 0;
    }

    if (depth <= 0) {
        return quiescence(position, alpha, beta, ply, context);
    }

    generate_legal_moves(position, &moves);
    order_moves(position, &moves, 1, context, ply);

    if (moves.count == 0) {
        int king_square = find_king(position, position->side_to_move);

        if (is_valid_square(king_square) &&
            is_square_attacked(
                position,
                king_square,
                opposite_color(position->side_to_move)
            )) {
            return -SEARCH_CHECKMATE + ply;
        }

        return 0;
    }

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        PrincipalVariation child_variation;
        UndoState undo;
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        score = -negamax(
            position,
            depth - 1,
            -beta,
            -alpha,
            ply + 1,
            &child_variation,
            context
        );
        undo_move(position, move, &undo);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (score >= beta) {
            record_killer_move(context, ply, move);
            update_variation(variation, move, &child_variation);
            return beta;
        }

        if (score > alpha) {
            alpha = score;
            update_variation(variation, move, &child_variation);
        }
    }

    return alpha;
}

static int search_position_with_variation(
    Position *position,
    int depth,
    PrincipalVariation *variation,
    SearchContext *context
) {
    MoveList moves;
    int alpha = -SEARCH_INFINITY;
    int beta = SEARCH_INFINITY;
    int index;

    clear_variation(variation);

    if (position == 0) {
        return 0;
    }

    if (search_has_stopped(context)) {
        return 0;
    }

    if (depth <= 0) {
        return evaluate_position(position);
    }

    generate_legal_moves(position, &moves);
    order_moves(position, &moves, 1, context, 0);

    if (moves.count == 0) {
        int king_square = find_king(position, position->side_to_move);

        if (is_valid_square(king_square) &&
            is_square_attacked(
                position,
                king_square,
                opposite_color(position->side_to_move)
            )) {
            return -SEARCH_CHECKMATE;
        }

        return 0;
    }

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        PrincipalVariation child_variation;
        UndoState undo;
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        score = -negamax(
            position,
            depth - 1,
            -beta,
            -alpha,
            1,
            &child_variation,
            context
        );
        undo_move(position, move, &undo);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (score > alpha) {
            alpha = score;
            update_variation(variation, move, &child_variation);
        }
    }

    return alpha;
}

int search_position(Position *position, int depth, Move *best_move) {
    PrincipalVariation variation;
    SearchContext context;
    int score;

    initialize_search_context(&context, 0);
    score = search_position_with_variation(position, depth, &variation, &context);

    if (best_move != 0) {
        best_move->from = NO_SQUARE;
        best_move->to = NO_SQUARE;
        best_move->promotion = PIECE_NONE;
        best_move->flags = MOVE_FLAG_NONE;

        if (variation.count > 0) {
            *best_move = variation.moves[0];
        }
    }

    return score;
}

int search_iterative(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth
) {
    Move move;
    PrincipalVariation current_variation;
    SearchContext context;
    int depth;
    int score = 0;

    if (best_move != 0) {
        best_move->from = NO_SQUARE;
        best_move->to = NO_SQUARE;
        best_move->promotion = PIECE_NONE;
        best_move->flags = MOVE_FLAG_NONE;
    }

    if (completed_depth != 0) {
        *completed_depth = 0;
    }

    clear_variation(variation);

    if (position == 0) {
        return 0;
    }

    if (maximum_depth <= 0) {
        return evaluate_position(position);
    }

    initialize_search_context(&context, time_limit_ms);

    score = evaluate_position(position);

    for (depth = 1; depth <= maximum_depth; ++depth) {
        score = search_position_with_variation(
            position,
            depth,
            &current_variation,
            &context
        );

        if (context.stopped) {
            break;
        }

        if (current_variation.count > 0) {
            move = current_variation.moves[0];

            if (best_move != 0) {
                *best_move = move;
            }

            if (variation != 0) {
                *variation = current_variation;
            }
        }

        if (completed_depth != 0) {
            *completed_depth = depth;
        }
    }

    return score;
}
