#include "search_internal.h"

#include "move_ordering.h"
#include "quiescence.h"

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

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
        return quiescence_search(position, alpha, beta, ply, context);
    }

    generate_legal_moves(position, &moves);
    order_moves(position, &moves, 1, context, ply);

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
