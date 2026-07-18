#include "search_internal.h"

#include "move_ordering.h"
#include "quiescence.h"

#include "src/chess/movegen.h"
#include "src/chess/zobrist.h"
#include "src/eval/evaluation.h"

#define REVERSE_FUTILITY_MARGIN 120
#define LATE_MOVE_PRUNING_START 8
#define MAX_CHECK_EXTENSION_PLY 8
#define MAX_LMR_DEPTH 64

static int lmr_reductions[MAX_LMR_DEPTH][MAX_MOVES];
static int lmr_reductions_initialized = 0;

static void initialize_lmr_reductions(void) {
    int depth;
    int move_index;

    if (lmr_reductions_initialized) {
        return;
    }

    for (depth = 0; depth < MAX_LMR_DEPTH; ++depth) {
        for (move_index = 0; move_index < MAX_MOVES; ++move_index) {
            int reduction = 0;

            if (depth >= 3 && move_index >= 4) {
                reduction = 1;
                if (depth >= 6 && move_index >= 8) {
                    reduction++;
                }
                if (depth >= 10 && move_index >= 16) {
                    reduction++;
                }
                if (reduction > depth - 2) {
                    reduction = depth - 2;
                }
            }
            lmr_reductions[depth][move_index] = reduction;
        }
    }

    lmr_reductions_initialized = 1;
}

static int late_move_reduction(int depth, int move_index) {
    initialize_lmr_reductions();

    if (depth >= MAX_LMR_DEPTH) {
        depth = MAX_LMR_DEPTH - 1;
    }
    if (move_index >= MAX_MOVES) {
        move_index = MAX_MOVES - 1;
    }
    return depth < 0 || move_index < 0 ? 0 : lmr_reductions[depth][move_index];
}

static int score_to_table(int score, int ply) {
    if (score > SEARCH_CHECKMATE - MAX_PRINCIPAL_VARIATION) {
        return score + ply;
    }

    if (score < -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION) {
        return score - ply;
    }

    return score;
}

static int score_from_table(int score, int ply) {
    if (score > SEARCH_CHECKMATE - MAX_PRINCIPAL_VARIATION) {
        return score - ply;
    }

    if (score < -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION) {
        return score + ply;
    }

    return score;
}

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

static int pawn_reaches_seventh_rank(const Position *position, Move move) {
    Piece piece = position_piece_at(position, move.from);
    int target_row = square_row(move.to);

    if (piece == PIECE_WHITE_PAWN) {
        return target_row == 1;
    }

    if (piece == PIECE_BLACK_PAWN) {
        return target_row == 6;
    }

    return 0;
}

static int negamax(
    Position *position,
    int depth,
    int alpha,
    int beta,
    int ply,
    int allow_null_move,
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

    search_record_node(context, ply, 0);

    if (search_has_stopped(context)) {
        return 0;
    }

    if (search_is_draw(context, position)) {
        return 0;
    }

    if (ply >= MAX_SEARCH_PLY - 1) {
        return evaluate_position(position);
    }

    if (alpha < -SEARCH_CHECKMATE + ply) {
        alpha = -SEARCH_CHECKMATE + ply;
    }
    if (beta > SEARCH_CHECKMATE - ply - 1) {
        beta = SEARCH_CHECKMATE - ply - 1;
    }
    if (alpha >= beta) {
        return alpha;
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

    if (allow_null_move && depth >= 3 && !in_check && beta < SEARCH_INFINITY &&
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
            0,
            &null_variation,
            context
        );
        search_pop_position(context);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (null_score >= beta) {
            if (depth >= 6) {
                PrincipalVariation verification_variation;
                int verification_score = -negamax(
                    position,
                    depth - 4,
                    -beta,
                    -beta + 1,
                    ply,
                    0,
                    &verification_variation,
                    context
                );

                if (search_has_stopped(context)) {
                    return 0;
                }

                if (verification_score < beta) {
                    goto skip_null_cutoff;
                }
            }

            return beta;
        }
    }

skip_null_cutoff:

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

        return score_from_table(table_score, ply);
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
        int promotes_pawn;
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        gives_check = move_gives_check(position, move);
        promotes_pawn = pawn_reaches_seventh_rank(position, move);

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
            int reduction = late_move_reduction(depth, index);

            if (reduction > 0) {
                search_depth -= reduction;
                reduced = 1;
                context->late_move_reductions++;
            }
        }

        if (gives_check && ply < MAX_CHECK_EXTENSION_PLY) {
            search_depth++;
        } else if (promotes_pawn && ply < MAX_CHECK_EXTENSION_PLY) {
            search_depth++;
        }

        if (index == 0) {
            score = -negamax(
                position, search_depth, -beta, -alpha, ply + 1, 1,
                &child_variation, context
            );
        } else {
            score = -negamax(
                position, search_depth, -alpha - 1, -alpha, ply + 1, 1,
                &child_variation, context
            );

            if (score > alpha && score < beta && reduced) {
                context->late_move_researches++;
                score = -negamax(
                    position, depth - 1, -alpha - 1, -alpha, ply + 1, 1,
                    &child_variation, context
                );
            }

            if (score > alpha && score < beta) {
                score = -negamax(
                    position, depth - 1, -beta, -alpha, ply + 1, 1,
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
            context->beta_cutoffs++;
            if (index == 0) {
                context->first_move_beta_cutoffs++;
            }
            record_quiet_failures(
                context,
                position->side_to_move,
                depth,
                &moves,
                index
            );
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
                score_to_table(beta, ply),
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
        score_to_table(alpha, ply),
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

    search_record_node(context, 0, 0);

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

        return score_from_table(table_score, 0);
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
        score_to_table(alpha, 0),
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
    initialize_lmr_reductions();
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

int search_iterative_with_callback_and_node_limit(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    uint64_t node_limit,
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
    initialize_lmr_reductions();
    search_set_node_limit(&context, node_limit);
    search_push_position(&context, position);

    if (best_move != 0) {
        MoveList legal_moves;

        generate_legal_moves(position, &legal_moves);
        if (legal_moves.count > 0) {
            *best_move = legal_moves.moves[0];
        }
    }

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
            SearchStatistics statistics;

            search_get_statistics(&context, &statistics);
            callback(depth, score, &current_variation, &statistics, user_data);
        }

        if (time_limit_ms > 0 &&
            search_elapsed_ms(&context) * 4 >= time_limit_ms * 3) {
            break;
        }
    }

    destroy_search_context(&context);
    return completed_score;
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
    return search_iterative_with_callback_and_node_limit(
        position,
        maximum_depth,
        time_limit_ms,
        0,
        best_move,
        variation,
        completed_depth,
        callback,
        user_data
    );
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
