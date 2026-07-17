#include "search_internal.h"

#include "move_ordering.h"
#include "quiescence.h"

#include "src/chess/movegen.h"
#include "src/chess/zobrist.h"
#include "src/eval/evaluation.h"

#define REVERSE_FUTILITY_MARGIN 120
#define LATE_MOVE_PRUNING_START 8
#define MAX_CHECK_EXTENSION_PLY 8

static int has_non_pawn_material(const Position *position, Color color) {
    int square;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        Piece piece = position_piece_at(position, square);
        PieceType type = piece_type(piece);

        if (piece_color(piece) == color &&
            type != PIECE_TYPE_PAWN && type != PIECE_TYPE_KING &&
            type != PIECE_TYPE_NONE) {
            return 1;
        }
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
    Move table_move;
    uint64_t key;
    int table_score;
    int in_check;
    int original_alpha = alpha;
    int index;

    clear_variation(variation);
    table_move.from = NO_SQUARE;
    table_move.to = NO_SQUARE;
    table_move.promotion = PIECE_NONE;
    table_move.flags = MOVE_FLAG_NONE;

    if (search_has_stopped(context)) {
        return 0;
    }

    if (search_is_draw(context, position)) {
        return 0;
    }

    if (depth <= 0) {
        return quiescence_search(position, alpha, beta, ply, context);
    }

    in_check = position_is_in_check(position);

    if (depth <= 2 && !in_check && beta < SEARCH_INFINITY) {
        int static_score = evaluate_position(position);

        if (static_score - depth * REVERSE_FUTILITY_MARGIN >= beta) {
            return beta;
        }
    }

    if (depth >= 3 && !in_check && beta < SEARCH_INFINITY &&
        has_non_pawn_material(position, position->side_to_move)) {
        Position null_position = *position;
        PrincipalVariation null_variation;
        int null_score;

        null_position.side_to_move = opposite_color(null_position.side_to_move);
        null_position.en_passant_square = NO_SQUARE;
        null_position.halfmove_clock++;
        search_push_position(context, &null_position);
        null_score = -negamax(
            &null_position,
            depth - 3,
            -beta,
            -beta + 1,
            ply + 1,
            &null_variation,
            context
        );
        search_pop_position(context);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (null_score >= beta) {
            return beta;
        }
    }

    key = position_key(position);
    if (probe_transposition_table(
            &context->table,
            key,
            depth,
            alpha,
            beta,
            &table_score,
            &table_move
        )) {
        if (is_valid_square(table_move.from) && is_valid_square(table_move.to)) {
            update_variation(variation, table_move, 0);
        }

        return table_score;
    }

    generate_legal_moves(position, &moves);
    order_moves(position, &moves, 1, context, ply, &table_move);

    if (moves.count == 0) {
        if (position_is_in_check(position)) {
            return -SEARCH_CHECKMATE + ply;
        }

        return 0;
    }

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        PrincipalVariation child_variation;
        UndoState undo;
        int search_depth = depth - 1;
        int reduced = 0;
        int gives_check;
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        gives_check = move_gives_check(position, move);

        if (depth <= 2 && !in_check &&
            index >= LATE_MOVE_PRUNING_START + depth * 4 &&
            (move.flags & MOVE_FLAG_CAPTURE) == 0 && !gives_check) {
            continue;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        search_push_position(context, position);

        if (depth >= 3 && index >= 4 &&
            (move.flags & MOVE_FLAG_CAPTURE) == 0 &&
            !gives_check) {
            search_depth--;
            reduced = 1;
        }

        if (gives_check && ply < MAX_CHECK_EXTENSION_PLY) {
            search_depth++;
        }

        if (index == 0) {
            score = -negamax(
                position, search_depth, -beta, -alpha, ply + 1,
                &child_variation, context
            );
        } else {
            score = -negamax(
                position, search_depth, -alpha - 1, -alpha, ply + 1,
                &child_variation, context
            );

            if (score > alpha && score < beta && reduced) {
                score = -negamax(
                    position, depth - 1, -alpha - 1, -alpha, ply + 1,
                    &child_variation, context
                );
            }

            if (score > alpha && score < beta) {
                score = -negamax(
                    position, depth - 1, -beta, -alpha, ply + 1,
                    &child_variation, context
                );
            }
        }
        search_pop_position(context);
        undo_move(position, move, &undo);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (score >= beta) {
            record_quiet_cutoff(
                context,
                position->side_to_move,
                ply,
                depth,
                move
            );
            update_variation(variation, move, &child_variation);
            store_transposition_table(
                &context->table,
                key,
                depth,
                beta,
                TRANSPOSITION_LOWER_BOUND,
                move
            );
            return beta;
        }

        if (score > alpha) {
            alpha = score;
            update_variation(variation, move, &child_variation);
        }
    }

    store_transposition_table(
        &context->table,
        key,
        depth,
        alpha,
        alpha <= original_alpha
            ? TRANSPOSITION_UPPER_BOUND
            : TRANSPOSITION_EXACT,
        variation->count > 0 ? variation->moves[0] : table_move
    );

    return alpha;
}

static int search_position_with_variation(
    Position *position,
    int depth,
    int alpha,
    int beta,
    PrincipalVariation *variation,
    SearchContext *context
) {
    MoveList moves;
    Move table_move;
    uint64_t key;
    int table_score;
    int index;

    clear_variation(variation);
    table_move.from = NO_SQUARE;
    table_move.to = NO_SQUARE;
    table_move.promotion = PIECE_NONE;
    table_move.flags = MOVE_FLAG_NONE;

    if (position == 0) {
        return 0;
    }

    if (search_has_stopped(context)) {
        return 0;
    }

    if (search_is_draw(context, position)) {
        return 0;
    }

    if (depth <= 0) {
        return evaluate_position(position);
    }

    key = position_key(position);
    if (probe_transposition_table(
            &context->table,
            key,
            depth,
            alpha,
            beta,
            &table_score,
            &table_move
        )) {
        if (is_valid_square(table_move.from) && is_valid_square(table_move.to)) {
            update_variation(variation, table_move, 0);
        }

        return table_score;
    }

    generate_legal_moves(position, &moves);
    order_moves(position, &moves, 1, context, 0, &table_move);

    if (moves.count == 0) {
        if (position_is_in_check(position)) {
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

        search_push_position(context, position);

        score = -negamax(
            position,
            depth - 1,
            -beta,
            -alpha,
            1,
            &child_variation,
            context
        );
        search_pop_position(context);
        undo_move(position, move, &undo);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (score > alpha) {
            alpha = score;
            update_variation(variation, move, &child_variation);
        }
    }

    store_transposition_table(
        &context->table,
        key,
        depth,
        alpha,
        TRANSPOSITION_EXACT,
        variation->count > 0 ? variation->moves[0] : table_move
    );

    return alpha;
}

int search_position(Position *position, int depth, Move *best_move) {
    PrincipalVariation variation;
    SearchContext context;
    int score;

    initialize_search_context(&context, 0);
    search_push_position(&context, position);
    score = search_position_with_variation(
        position,
        depth,
        -SEARCH_INFINITY,
        SEARCH_INFINITY,
        &variation,
        &context
    );

    if (best_move != 0) {
        best_move->from = NO_SQUARE;
        best_move->to = NO_SQUARE;
        best_move->promotion = PIECE_NONE;
        best_move->flags = MOVE_FLAG_NONE;

        if (variation.count > 0) {
            *best_move = variation.moves[0];
        }
    }

    destroy_search_context(&context);

    return score;
}

int search_iterative_with_callback(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
) {
    Move move;
    PrincipalVariation current_variation;
    SearchContext context;
    int depth;
    int score = 0;
    int completed_score;

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
    search_push_position(&context, position);

    completed_score = evaluate_position(position);

    for (depth = 1; depth <= maximum_depth; ++depth) {
        int alpha = -SEARCH_INFINITY;
        int beta = SEARCH_INFINITY;

        if (depth > 1) {
            alpha = completed_score - 50;
            beta = completed_score + 50;
        }

        score = search_position_with_variation(
            position,
            depth,
            alpha,
            beta,
            &current_variation,
            &context
        );

        if (context.stopped) {
            break;
        }

        if (score <= alpha || score >= beta) {
            score = search_position_with_variation(
                position,
                depth,
                -SEARCH_INFINITY,
                SEARCH_INFINITY,
                &current_variation,
                &context
            );

            if (context.stopped) {
                break;
            }
        }

        completed_score = score;

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

        if (callback != 0) {
            callback(depth, score, &current_variation, user_data);
        }
    }

    destroy_search_context(&context);
    return completed_score;
}

int search_iterative(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth
) {
    return search_iterative_with_callback(
        position,
        maximum_depth,
        time_limit_ms,
        best_move,
        variation,
        completed_depth,
        0,
        0
    );
}
