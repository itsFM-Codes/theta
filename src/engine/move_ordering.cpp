#include "move_ordering.h"

#include "static_exchange.h"

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

#define CHECK_MOVE_SCORE 5000
#define KILLER_MOVE_SCORE 4000
#define COUNTERMOVE_SCORE 3500
#define MAX_HISTORY_SCORE 3000
#define TABLE_MOVE_SCORE 20000
#define GOOD_CAPTURE_SCORE 10000
#define LOSING_CAPTURE_SCORE 1500

static void update_history_score(int *score, int bonus) {
    if (score == 0) {
        return;
    }

    *score += bonus - *score * (bonus < 0 ? -bonus : bonus) /
        MAX_HISTORY_SCORE;
    if (*score > MAX_HISTORY_SCORE) {
        *score = MAX_HISTORY_SCORE;
    } else if (*score < -MAX_HISTORY_SCORE) {
        *score = -MAX_HISTORY_SCORE;
    }

}

static int moves_are_equal(Move first, Move second) {
    return first.from == second.from &&
           first.to == second.to &&
           first.promotion == second.promotion &&
           first.flags == second.flags;
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

static int history_move_score(
    const SearchContext *context,
    Color color,
    Move move
) {
    if (context == 0 || color == COLOR_NONE) {
        return 0;
    }

    return context->history[color][move.from][move.to];
}

static int counter_move_score(
    const SearchContext *context,
    int ply,
    Move move
) {
    Move previous_move;
    Move counter_move;

    if (context == 0 || ply <= 0 || ply > MAX_SEARCH_PLY) {
        return 0;
    }

    previous_move = context->line_moves[ply - 1];
    if (!is_valid_square(previous_move.from) ||
        !is_valid_square(previous_move.to)) {
        return 0;
    }

    counter_move = context->counter_moves[previous_move.from][previous_move.to];
    return moves_are_equal(move, counter_move) ? COUNTERMOVE_SCORE : 0;
}

static Piece captured_piece_for_move(const Position *position, Move move) {
    if (move.flags & MOVE_FLAG_EN_PASSANT) {
        return position->side_to_move == COLOR_WHITE
            ? PIECE_BLACK_PAWN
            : PIECE_WHITE_PAWN;
    }

    return position_piece_at(position, move.to);
}

static int capture_history_score(
    const SearchContext *context,
    Color color,
    PieceType attacker_type,
    int target_square,
    PieceType captured_type
) {
    if (context == 0 || color == COLOR_NONE ||
        attacker_type == PIECE_TYPE_NONE ||
        captured_type == PIECE_TYPE_NONE ||
        !is_valid_square(target_square)) {
        return 0;
    }

    return context->capture_history[color][attacker_type][target_square]
        [captured_type];
}

static int move_order_score(
    Position *position,
    Move move,
    int order_checks,
    const SearchContext *context,
    int ply,
    const Move *table_move
) {
    Piece attacker;
    Piece victim;

    if (table_move != 0 && moves_are_equal(move, *table_move)) {
        return TABLE_MOVE_SCORE;
    }

    if ((move.flags & MOVE_FLAG_CAPTURE) == 0) {
        int killer_score;
        int reply_score;

        if (order_checks && move_gives_check(position, move)) {
            return CHECK_MOVE_SCORE;
        }

        killer_score = killer_move_score(context, ply, move);
        if (killer_score > 0) {
            return killer_score;
        }

        reply_score = counter_move_score(context, ply, move);
        if (reply_score > 0) {
            return reply_score;
        }

        return history_move_score(context, position->side_to_move, move);
    }

    attacker = position_piece_at(position, move.from);
    victim = captured_piece_for_move(position, move);
    {
        int see_score = static_exchange_evaluation(position, move);
        int history_score = capture_history_score(
            context,
            position->side_to_move,
            piece_type(attacker),
            move.to,
            piece_type(victim)
        );
        int value_score = piece_value(victim) - piece_value(attacker);

        if (see_score >= 0) {
            return GOOD_CAPTURE_SCORE + see_score * 16 + value_score +
                   history_score;
        }

        return LOSING_CAPTURE_SCORE + see_score * 8 + value_score +
               history_score;
    }
}

void order_moves(
    Position *position,
    MoveList *moves,
    int order_checks,
    const SearchContext *context,
    int ply,
    const Move *table_move
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
            ply,
            table_move
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

void record_quiet_cutoff(
    SearchContext *context,
    Color color,
    int ply,
    int depth,
    Move move
) {
    int bonus;
    int *history_score;

    if (context == 0 || ply < 0 || ply >= MAX_KILLER_PLY ||
        (move.flags & MOVE_FLAG_CAPTURE) != 0) {
        return;
    }

    if (!moves_are_equal(move, context->killer_moves[ply][0])) {
        context->killer_moves[ply][1] = context->killer_moves[ply][0];
        context->killer_moves[ply][0] = move;
    }

    if (ply > 0 && ply <= MAX_SEARCH_PLY) {
        Move previous_move = context->line_moves[ply - 1];

        if (is_valid_square(previous_move.from) &&
            is_valid_square(previous_move.to)) {
            context->counter_moves[previous_move.from][previous_move.to] = move;
        }
    }

    if (color == COLOR_NONE) {
        return;
    }

    bonus = depth * depth;
    history_score = &context->history[color][move.from][move.to];
    update_history_score(history_score, bonus);
}

void record_quiet_failures(
    SearchContext *context,
    Color color,
    int depth,
    const MoveList *moves,
    int count
) {
    int index;
    int penalty;

    if (context == 0 || color == COLOR_NONE || moves == 0 || depth <= 0) {
        return;
    }

    penalty = -(depth * depth);
    for (index = 0; index < count && index < moves->count; ++index) {
        Move move = moves->moves[index];

        if ((move.flags & MOVE_FLAG_CAPTURE) == 0) {
            update_history_score(
                &context->history[color][move.from][move.to],
                penalty
            );
        }
    }
}

void record_capture_cutoff(
    SearchContext *context,
    const Position *position,
    Color color,
    int depth,
    Move move
) {
    Piece attacker;
    Piece victim;
    int *history_score;

    if (context == 0 || position == 0 || color == COLOR_NONE ||
        depth <= 0 || (move.flags & MOVE_FLAG_CAPTURE) == 0) {
        return;
    }

    attacker = position_piece_at(position, move.from);
    victim = captured_piece_for_move(position, move);
    if (piece_type(attacker) == PIECE_TYPE_NONE ||
        piece_type(victim) == PIECE_TYPE_NONE) {
        return;
    }

    history_score = &context->capture_history[color][piece_type(attacker)]
        [move.to][piece_type(victim)];
    update_history_score(history_score, depth * depth);
}

void record_capture_failures(
    SearchContext *context,
    const Position *position,
    Color color,
    int depth,
    const MoveList *moves,
    int count
) {
    int index;
    int penalty;

    if (context == 0 || position == 0 || color == COLOR_NONE ||
        moves == 0 || depth <= 0) {
        return;
    }

    penalty = -(depth * depth);
    for (index = 0; index < count && index < moves->count; ++index) {
        Move move = moves->moves[index];
        Piece attacker;
        Piece victim;

        if ((move.flags & MOVE_FLAG_CAPTURE) == 0) {
            continue;
        }

        attacker = position_piece_at(position, move.from);
        victim = captured_piece_for_move(position, move);
        if (piece_type(attacker) == PIECE_TYPE_NONE ||
            piece_type(victim) == PIECE_TYPE_NONE) {
            continue;
        }

        update_history_score(
            &context->capture_history[color][piece_type(attacker)]
                [move.to][piece_type(victim)],
            penalty
        );
    }
}
