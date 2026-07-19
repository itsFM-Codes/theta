#include "search_internal.h"

#include "move_ordering.h"
#include "quiescence.h"

#include "src/chess/movegen.h"
#include "src/chess/zobrist.h"
#include "src/eval/evaluation.h"

#define REVERSE_FUTILITY_MARGIN 120
#define LATE_MOVE_PRUNING_START 8
#define STATIC_FUTILITY_MARGIN 95
#define RAZORING_MARGIN 180
#define SEARCH_HISTORY_LIMIT 3000
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

int search_score_to_table(int score, int ply) {
    if (score > SEARCH_CHECKMATE - MAX_PRINCIPAL_VARIATION) {
        return score + ply;
    }

    if (score < -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION) {
        return score - ply;
    }

    return score;
}

int search_score_from_table(int score, int ply) {
    if (score > SEARCH_CHECKMATE - MAX_PRINCIPAL_VARIATION) {
        return score - ply;
    }

    if (score < -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION) {
        return score + ply;
    }

    return score;
}

static int has_null_move_material(const Position *position, Color color) {
    int minor_count = 0;
    int square;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        Piece piece = position_piece_at(position, square);
        PieceType type = piece_type(piece);

        if (piece_color(piece) != color) {
            continue;
        }

        if (type == PIECE_TYPE_QUEEN || type == PIECE_TYPE_ROOK) {
            return 1;
        }

        if (type == PIECE_TYPE_BISHOP || type == PIECE_TYPE_KNIGHT) {
            minor_count++;
        }
    }

    return minor_count >= 2;
}

static int move_is_quiet(Move move) {
    return (move.flags & (MOVE_FLAG_CAPTURE | MOVE_FLAG_PROMOTION)) == 0;
}

static int static_futility_margin(int depth, int improving) {
    int margin = STATIC_FUTILITY_MARGIN * depth;

    return improving ? margin + 45 : margin;
}

static int late_move_pruning_threshold(
    int depth,
    int is_pv_node,
    int improving
) {
    int threshold = LATE_MOVE_PRUNING_START + depth * 4;

    if (!is_pv_node) {
        threshold -= 2;
    }
    if (improving) {
        threshold += 3;
    }
    if (threshold < 4) {
        threshold = 4;
    }

    return threshold;
}

static int null_move_reduction(int depth, int static_score, int beta) {
    int reduction = 2 + depth / 4;
    int margin = static_score - beta;

    if (margin > 300) {
        reduction++;
    }
    if (margin > 600) {
        reduction++;
    }
    if (reduction > depth - 1) {
        reduction = depth - 1;
    }

    return reduction;
}

static int search_static_evaluation(
    Position *position,
    SearchContext *context,
    int ply
) {
    int score;

    if (context != 0 &&
        probe_transposition_static_evaluation(
            &context->table,
            position_key(position),
            &score
        )) {
        if (ply >= 0 && ply < MAX_SEARCH_PLY) {
            context->static_evaluations[ply] = score;
            context->static_evaluation_valid[ply] = 1;
        }

        return score;
    }

    score = evaluate_position(position);

    if (context != 0 && ply >= 0 && ply < MAX_SEARCH_PLY) {
        context->static_evaluations[ply] = score;
        context->static_evaluation_valid[ply] = 1;
    }

    return score;
}

static int position_is_improving(
    const SearchContext *context,
    int ply,
    int static_score
) {
    if (context == 0 || ply < 2 || ply >= MAX_SEARCH_PLY ||
        !context->static_evaluation_valid[ply - 2]) {
        return 0;
    }

    return static_score > context->static_evaluations[ply - 2];
}

static int adjusted_late_move_reduction(
    const SearchContext *context,
    Color moving_color,
    int depth,
    int move_index,
    int is_pv_node,
    int improving,
    Move move
) {
    int reduction = late_move_reduction(depth, move_index);
    int history_score = 0;

    if (reduction <= 0) {
        return 0;
    }

    if (is_pv_node) {
        reduction--;
    }
    if (improving) {
        reduction--;
    } else {
        reduction++;
    }

    if (context != 0 && moving_color != COLOR_NONE) {
        history_score = context->history[moving_color][move.from][move.to];
    }

    if (history_score > SEARCH_HISTORY_LIMIT / 3) {
        reduction--;
    } else if (history_score < -SEARCH_HISTORY_LIMIT / 3) {
        reduction++;
    }

    if (reduction < 0) {
        reduction = 0;
    }
    if (reduction > depth - 2) {
        reduction = depth - 2;
    }

    return reduction;
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
    int is_pv_node,
    PrincipalVariation *variation,
    SearchContext *context
) {
    MoveList moves;
    Move table_move;
    uint64_t key;
    int table_score;
    int in_check;
    int static_score;
    int improving;
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
    static_score = search_static_evaluation(position, context, ply);
    improving = position_is_improving(context, ply, static_score);

    if (depth <= 2 && !in_check &&
        alpha > -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION &&
        static_score + RAZORING_MARGIN * depth < alpha) {
        int razor_score = quiescence_search(
            position,
            alpha - 1,
            alpha,
            ply,
            context
        );

        if (search_has_stopped(context)) {
            return 0;
        }

        if (razor_score <= alpha) {
            if (context != 0) {
                context->razoring_prunes++;
            }
            return razor_score;
        }
    }

    if (depth <= 2 && !in_check && beta < SEARCH_INFINITY) {
        if (static_score - depth * REVERSE_FUTILITY_MARGIN >= beta) {
            if (context != 0) {
                context->reverse_futility_prunes++;
            }
            return beta;
        }
    }

    if (allow_null_move && depth >= 3 && !in_check && beta < SEARCH_INFINITY &&
        has_null_move_material(position, position->side_to_move)) {
        Position null_position = *position;
        PrincipalVariation null_variation;
        int reduction = null_move_reduction(depth, static_score, beta);
        int null_score;

        if (context != 0) {
            context->null_move_attempts++;
        }
        null_position.side_to_move = opposite_color(null_position.side_to_move);
        null_position.en_passant_square = NO_SQUARE;
        null_position.halfmove_clock++;
        search_push_position(context, &null_position);
        null_score = -negamax(
            &null_position,
            depth - 1 - reduction,
            -beta,
            -beta + 1,
            ply + 1,
            0,
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

            if (context != 0) {
                context->null_move_cutoffs++;
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

        return search_score_from_table(table_score, ply);
    }

    if (!is_pv_node && depth >= 5 && !in_check &&
        (!is_valid_square(table_move.from) ||
         !is_valid_square(table_move.to))) {
        depth--;
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
        int quiet_move;
        Color moving_color;
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        gives_check = move_gives_check(position, move);
        promotes_pawn = pawn_reaches_seventh_rank(position, move);
        quiet_move = move_is_quiet(move);
        moving_color = position->side_to_move;

        if (depth <= 2 && !in_check &&
            index >= late_move_pruning_threshold(
                depth,
                is_pv_node,
                improving
            ) &&
            quiet_move && !gives_check) {
            if (context != 0) {
                context->late_move_prunes++;
            }
            continue;
        }

        if (depth <= 3 && !is_pv_node && !in_check && index > 0 &&
            quiet_move && !gives_check &&
            alpha > -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION &&
            static_score + static_futility_margin(depth, improving) <= alpha) {
            if (context != 0) {
                context->static_futility_prunes++;
            }
            continue;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        search_push_position(context, position);
        if (context != 0 && ply >= 0 && ply < MAX_SEARCH_PLY) {
            context->line_moves[ply] = move;
        }

        if (depth >= 3 && index >= 4 &&
            quiet_move &&
            !gives_check) {
            int reduction = adjusted_late_move_reduction(
                context,
                moving_color,
                depth,
                index,
                is_pv_node,
                improving,
                move
            );

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
                is_pv_node,
                &child_variation, context
            );
        } else {
            score = -negamax(
                position, search_depth, -alpha - 1, -alpha, ply + 1, 1,
                0,
                &child_variation, context
            );

            if (score > alpha && score < beta && reduced) {
                context->late_move_researches++;
                score = -negamax(
                    position, depth - 1, -alpha - 1, -alpha, ply + 1, 1,
                    0,
                    &child_variation, context
                );
            }

            if (score > alpha && score < beta) {
                score = -negamax(
                    position, depth - 1, -beta, -alpha, ply + 1, 1,
                    is_pv_node,
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
            record_capture_failures(
                context,
                position,
                position->side_to_move,
                depth,
                &moves,
                index
            );
            if (quiet_move) {
                record_quiet_cutoff(
                    context,
                    position->side_to_move,
                    ply,
                    depth,
                    move
                );
            } else {
                record_capture_cutoff(
                    context,
                    position,
                    position->side_to_move,
                    depth,
                    move
                );
            }
            update_variation(variation, move, &child_variation);
            store_transposition_table_with_static_evaluation(
                &context->table,
                key,
                depth,
                search_score_to_table(beta, ply),
                TRANSPOSITION_LOWER_BOUND,
                move,
                static_score
            );
            return beta;
        }

        if (score > alpha) {
            alpha = score;
            update_variation(variation, move, &child_variation);
        }
    }

    store_transposition_table_with_static_evaluation(
        &context->table,
        key,
        depth,
        search_score_to_table(alpha, ply),
        alpha <= original_alpha
            ? TRANSPOSITION_UPPER_BOUND
            : TRANSPOSITION_EXACT,
        variation->count > 0 ? variation->moves[0] : table_move,
        static_score
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
    int original_alpha = alpha;
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

        return search_score_from_table(table_score, 0);
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
        int quiet_move = move_is_quiet(move);
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        search_push_position(context, position);
        if (context != 0) {
            context->line_moves[0] = move;
        }

        if (index == 0) {
            score = -negamax(
                position,
                depth - 1,
                -beta,
                -alpha,
                1,
                1,
                1,
                &child_variation,
                context
            );
        } else {
            score = -negamax(
                position,
                depth - 1,
                -alpha - 1,
                -alpha,
                1,
                1,
                0,
                &child_variation,
                context
            );

            if (score > alpha && score < beta) {
                score = -negamax(
                    position,
                    depth - 1,
                    -beta,
                    -alpha,
                    1,
                    1,
                    1,
                    &child_variation,
                    context
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
            record_capture_failures(
                context,
                position,
                position->side_to_move,
                depth,
                &moves,
                index
            );
            if (quiet_move) {
                record_quiet_cutoff(
                    context,
                    position->side_to_move,
                    0,
                    depth,
                    move
                );
            } else {
                record_capture_cutoff(
                    context,
                    position,
                    position->side_to_move,
                    depth,
                    move
                );
            }
            update_variation(variation, move, &child_variation);
            store_transposition_table(
                &context->table,
                key,
                depth,
                search_score_to_table(beta, 0),
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
        search_score_to_table(alpha, 0),
        alpha <= original_alpha
            ? TRANSPOSITION_UPPER_BOUND
            : TRANSPOSITION_EXACT,
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
    SearchLimits limits = {};

    limits.soft_time_ms = time_limit_ms;
    limits.hard_time_ms = time_limit_ms;
    limits.node_limit = node_limit;
    limits.poll_interval = DEFAULT_SEARCH_POLL_INTERVAL;
    return search_iterative_with_limits(
        position,
        maximum_depth,
        &limits,
        best_move,
        variation,
        completed_depth,
        callback,
        user_data
    );
}

static int moves_are_equal(Move first, Move second) {
    return first.from == second.from &&
           first.to == second.to &&
           first.promotion == second.promotion &&
           first.flags == second.flags;
}

int search_iterative_with_limits(
    Position *position,
    int maximum_depth,
    const SearchLimits *limits,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
) {
    Move move;
    Move previous_best_move;
    PrincipalVariation current_variation;
    SearchContext context;
    int depth;
    int score = 0;
    int completed_score;
    int last_score_delta = 0;
    int previous_iteration_ms = 0;
    int stable_best_move_count = 0;
    int soft_time_limit_ms = limits == 0 ? 0 : limits->soft_time_ms;

    previous_best_move.from = NO_SQUARE;
    previous_best_move.to = NO_SQUARE;
    previous_best_move.promotion = PIECE_NONE;
    previous_best_move.flags = MOVE_FLAG_NONE;

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

    initialize_search_context(&context, limits == 0 ? 0 : limits->hard_time_ms);
    initialize_lmr_reductions();
    search_set_limits(&context, limits);
    if (limits != 0) {
        search_set_position_history(
            &context,
            limits->game_history,
            limits->game_history_count
        );
    }
    if (context.position_key_count == 0 ||
        context.position_keys[context.position_key_count - 1] !=
            position_key(position)) {
        search_push_position(&context, position);
    }

    if (best_move != 0) {
        MoveList legal_moves;

        generate_legal_moves(position, &legal_moves);
        if (legal_moves.count > 0) {
            *best_move = legal_moves.moves[0];
        }
    }

    completed_score = evaluate_position(position);

    for (depth = 1; depth <= maximum_depth; ++depth) {
        int iteration_start_ms = search_elapsed_ms(&context);
        int alpha = -SEARCH_INFINITY;
        int beta = SEARCH_INFINITY;

        if (depth > 1) {
            int window = 35 + depth * 5 + last_score_delta / 2;

            if (window > 200) {
                window = 200;
            }

            alpha = completed_score - window;
            beta = completed_score + window;
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
            context.aspiration_failures++;
            context.aspiration_researches++;
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

        last_score_delta = score > completed_score
            ? score - completed_score
            : completed_score - score;
        completed_score = score;

        if (current_variation.count > 0) {
            move = current_variation.moves[0];

            if (moves_are_equal(move, previous_best_move)) {
                stable_best_move_count++;
            } else {
                stable_best_move_count = 0;
            }
            previous_best_move = move;

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

        previous_iteration_ms = search_elapsed_ms(&context) - iteration_start_ms;

        if (soft_time_limit_ms > 0) {
            int elapsed_ms = search_elapsed_ms(&context);
            int allocated_ms = soft_time_limit_ms;
            int hard_time_limit_ms = limits == 0 ? 0 : limits->hard_time_ms;
            int estimated_next_iteration_ms = previous_iteration_ms > 0
                ? previous_iteration_ms * 2
                : 1;

            if (stable_best_move_count == 0) {
                allocated_ms += soft_time_limit_ms / 2;
            } else if (stable_best_move_count == 1) {
                allocated_ms += soft_time_limit_ms / 4;
            }
            if (last_score_delta > 100) {
                allocated_ms += soft_time_limit_ms / 2;
            } else if (last_score_delta > 40) {
                allocated_ms += soft_time_limit_ms / 4;
            }
            if (hard_time_limit_ms > 0 && allocated_ms > hard_time_limit_ms) {
                allocated_ms = hard_time_limit_ms;
            }

            if (elapsed_ms >= allocated_ms ||
                elapsed_ms + estimated_next_iteration_ms > allocated_ms) {
                break;
            }
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
