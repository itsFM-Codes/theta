#include "search_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <new>
#include <string.h>

#include "src/chess/movegen.h"
#include "src/chess/zobrist.h"
#include "src/eval/evaluation.h"

static void clear_move(Move *move) {
    move->from = NO_SQUARE;
    move->to = NO_SQUARE;
    move->promotion = PIECE_NONE;
    move->flags = MOVE_FLAG_NONE;
}

void initialize_search_context(
    SearchContext *context,
    SearchSharedState *shared_state,
    int time_limit_ms
) {
    int ply;
    int from;
    int to;

    context->start_time = std::chrono::steady_clock::now();
    context->hard_time_limit_ms = time_limit_ms;
    context->stopped = 0;
    context->stop_requested = 0;
    context->nodes = 0;
    context->node_limit = 0;
    context->poll_interval = DEFAULT_SEARCH_POLL_INTERVAL;
    context->next_poll_node = 0;
    context->quiescence_nodes = 0;
    context->beta_cutoffs = 0;
    context->first_move_beta_cutoffs = 0;
    context->null_move_attempts = 0;
    context->null_move_cutoffs = 0;
    context->reverse_futility_prunes = 0;
    context->late_move_prunes = 0;
    context->static_futility_prunes = 0;
    context->razoring_prunes = 0;
    context->delta_prunes = 0;
    context->see_prunes = 0;
    context->quiescence_check_moves = 0;
    context->late_move_reductions = 0;
    context->late_move_researches = 0;
    context->aspiration_failures = 0;
    context->aspiration_researches = 0;
    context->probcut_attempts = 0;
    context->probcut_cutoffs = 0;
    context->singular_attempts = 0;
    context->singular_extensions = 0;
    context->static_evaluation_calls = 0;
    context->static_evaluation_cache_hits = 0;
    context->raw_evaluations = 0;
    context->move_generations = 0;
    context->tactical_move_generations = 0;
    context->legal_move_attempts = 0;
    context->see_calls = 0;
    context->selective_depth = 0;
    context->position_key_count = 0;
    context->draw_score = 0;
    context->root_move_offset = 0;
    context->nnue_state_active = 0;
    context->nnue_states = new (std::nothrow) NnueState[MAX_SEARCH_PLY];
    if (context->nnue_states != 0) {
        memset(
            context->nnue_states,
            0,
            sizeof(NnueState) * MAX_SEARCH_PLY
        );
    }
    context->classical_state_active = 0;
    context->classical_states = new (std::nothrow)
        ClassicalEvalState[MAX_SEARCH_PLY];
    if (context->classical_states != 0) {
        memset(
            context->classical_states,
            0,
            sizeof(ClassicalEvalState) * MAX_SEARCH_PLY
        );
    }
    memset(context->static_evaluation_valid, 0,
           sizeof(context->static_evaluation_valid));
    memset(context->classical_pending_valid, 0,
           sizeof(context->classical_pending_valid));
    context->shared_state = shared_state;
    reset_zobrist_statistics();
    memset(
        &context->transposition_statistics,
        0,
        sizeof(context->transposition_statistics)
    );

    for (ply = 0; ply < MAX_KILLER_PLY; ++ply) {
        clear_move(&context->killer_moves[ply][0]);
        clear_move(&context->killer_moves[ply][1]);
    }

    for (from = 0; from < SQUARE_COUNT; ++from) {
        for (to = 0; to < SQUARE_COUNT; ++to) {
            clear_move(&context->counter_moves[from][to]);
        }
    }

    for (ply = 0; ply < MAX_SEARCH_PLY; ++ply) {
        clear_move(&context->line_moves[ply]);
        context->line_move_types[ply] = PIECE_TYPE_NONE;
        context->static_evaluations[ply] = 0;
    }
}

void destroy_search_context(SearchContext *context) {
    // Context borrows shared state.
    if (context != 0) {
        delete[] context->nnue_states;
        context->nnue_states = 0;
        delete[] context->classical_states;
        context->classical_states = 0;
    }
}

int search_has_stopped(SearchContext *context) {
    int elapsed;

    if (context == 0) {
        return 0;
    }

    if (context->stopped) {
        return 1;
    }

    if (context->nodes < context->next_poll_node) {
        return 0;
    }

    context->next_poll_node = context->nodes + context->poll_interval;

    if (context->stop_requested != 0 &&
        context->stop_requested->load(std::memory_order_relaxed)) {
        context->stopped = 1;
        return 1;
    }

    if (context->hard_time_limit_ms <= 0) {
        return 0;
    }

    elapsed = search_elapsed_ms(context);
    if (elapsed >= context->hard_time_limit_ms) {
        context->stopped = 1;
        return 1;
    }

    return 0;
}

int search_elapsed_ms(const SearchContext *context) {
    if (context == 0) {
        return 0;
    }

    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - context->start_time
    ).count();
}

void search_record_node(SearchContext *context, int ply, int is_quiescence) {
    if (context == 0) {
        return;
    }

    context->nodes++;
    if (context->node_limit > 0 && context->nodes >= context->node_limit) {
        context->stopped = 1;
    }
    if (is_quiescence) {
        context->quiescence_nodes++;
    }
    if (ply > context->selective_depth) {
        context->selective_depth = ply;
    }
}

void search_set_node_limit(SearchContext *context, uint64_t node_limit) {
    if (context != 0) {
        context->node_limit = node_limit;
    }
}

void search_set_limits(SearchContext *context, const SearchLimits *limits) {
    if (context == 0 || limits == 0) {
        return;
    }

    context->hard_time_limit_ms = limits->hard_time_ms;
    context->node_limit = limits->node_limit;
    context->poll_interval = limits->poll_interval > 0
        ? limits->poll_interval
        : DEFAULT_SEARCH_POLL_INTERVAL;
    context->stop_requested = limits->stop_requested;
    context->draw_score = limits->draw_score;
    context->root_move_offset = limits->root_move_offset;
    if (context->root_move_offset < 0) {
        context->root_move_offset = 0;
    }
}

void search_set_position_history(
    SearchContext *context,
    const uint64_t *keys,
    int count
) {
    int index;

    if (context == 0) {
        return;
    }

    context->position_key_count = 0;
    if (keys == 0 || count <= 0) {
        return;
    }

    if (count > MAX_POSITION_HISTORY - MAX_SEARCH_PLY) {
        keys += count - (MAX_POSITION_HISTORY - MAX_SEARCH_PLY);
        count = MAX_POSITION_HISTORY - MAX_SEARCH_PLY;
    }

    for (index = 0; index < count; ++index) {
        context->position_keys[index] = keys[index];
    }
    context->position_key_count = count;
}

void search_get_statistics(
    const SearchContext *context,
    SearchStatistics *statistics
) {
    if (statistics == 0) {
        return;
    }

    statistics->nodes = context == 0 ? 0 : context->nodes;
    statistics->quiescence_nodes = context == 0 ? 0 : context->quiescence_nodes;
    const TranspositionTable *table = context == 0 ||
        context->shared_state == 0
        ? 0
        : &context->shared_state->transposition_table;

    statistics->transposition_probes = context == 0
        ? 0
        : context->transposition_statistics.probes;
    statistics->transposition_key_hits = context == 0
        ? 0
        : context->transposition_statistics.key_hits;
    statistics->transposition_cutoffs = context == 0
        ? 0
        : context->transposition_statistics.score_cutoffs;
    statistics->transposition_stores = context == 0
        ? 0
        : context->transposition_statistics.stores;
    statistics->beta_cutoffs = context == 0 ? 0 : context->beta_cutoffs;
    statistics->first_move_beta_cutoffs = context == 0
        ? 0
        : context->first_move_beta_cutoffs;
    statistics->null_move_attempts = context == 0
        ? 0
        : context->null_move_attempts;
    statistics->null_move_cutoffs = context == 0
        ? 0
        : context->null_move_cutoffs;
    statistics->reverse_futility_prunes = context == 0
        ? 0
        : context->reverse_futility_prunes;
    statistics->late_move_prunes = context == 0 ? 0 : context->late_move_prunes;
    statistics->static_futility_prunes = context == 0
        ? 0
        : context->static_futility_prunes;
    statistics->razoring_prunes = context == 0 ? 0 : context->razoring_prunes;
    statistics->delta_prunes = context == 0 ? 0 : context->delta_prunes;
    statistics->see_prunes = context == 0 ? 0 : context->see_prunes;
    statistics->quiescence_check_moves = context == 0
        ? 0
        : context->quiescence_check_moves;
    statistics->late_move_reductions = context == 0
        ? 0
        : context->late_move_reductions;
    statistics->late_move_researches = context == 0
        ? 0
        : context->late_move_researches;
    statistics->aspiration_failures = context == 0
        ? 0
        : context->aspiration_failures;
    statistics->aspiration_researches = context == 0
        ? 0
        : context->aspiration_researches;
    statistics->probcut_attempts = context == 0
        ? 0
        : context->probcut_attempts;
    statistics->probcut_cutoffs = context == 0
        ? 0
        : context->probcut_cutoffs;
    statistics->singular_attempts = context == 0
        ? 0
        : context->singular_attempts;
    statistics->singular_extensions = context == 0
        ? 0
        : context->singular_extensions;
    statistics->static_evaluation_calls = context == 0
        ? 0
        : context->static_evaluation_calls;
    statistics->static_evaluation_cache_hits = context == 0
        ? 0
        : context->static_evaluation_cache_hits;
    statistics->raw_evaluations = context == 0 ? 0 : context->raw_evaluations;
    statistics->move_generations = context == 0
        ? 0
        : context->move_generations;
    statistics->tactical_move_generations = context == 0
        ? 0
        : context->tactical_move_generations;
    statistics->legal_move_attempts = context == 0
        ? 0
        : context->legal_move_attempts;
    statistics->see_calls = context == 0 ? 0 : context->see_calls;
    statistics->selective_depth = context == 0 ? 0 : context->selective_depth;
    statistics->elapsed_ms = search_elapsed_ms(context);
    statistics->hashfull = context == 0
        ? 0
        : transposition_table_hashfull(table);
    if (context == 0) {
        statistics->zobrist_cache_hits = 0;
        statistics->zobrist_rebuilds = 0;
    } else {
        get_zobrist_statistics(
            &statistics->zobrist_cache_hits,
            &statistics->zobrist_rebuilds
        );
    }
}

int search_push_position(SearchContext *context, const Position *position) {
    if (context == 0 || position == 0 ||
        context->position_key_count >= MAX_POSITION_HISTORY) {
        return 0;
    }

    context->position_keys[context->position_key_count] = position_key(position);
    context->position_key_count++;
    return 1;
}

void search_pop_position(SearchContext *context) {
    if (context != 0 && context->position_key_count > 0) {
        context->position_key_count--;
    }
}

int search_is_draw(const SearchContext *context, const Position *position) {
    uint64_t key;
    int matches = 0;
    int first_index;
    int index;

    if (position == 0) {
        return 0;
    }

    if (position->halfmove_clock >= 100) {
        return 1;
    }

    if (position_has_insufficient_material(position)) {
        return 1;
    }

    if (context == 0 || context->position_key_count < 3) {
        return 0;
    }

    key = position_key(position);

    first_index = context->position_key_count - 1 - position->halfmove_clock;
    if (first_index < 0) {
        first_index = 0;
    }

    for (index = context->position_key_count - 1;
         index >= first_index;
         --index) {
        if (context->position_keys[index] == key) {
            ++matches;
        }
    }

    return matches >= 3;
}

int search_draw_score(const SearchContext *context) {
    return context == 0 ? 0 : context->draw_score;
}

int search_evaluate_position(
    SearchContext *context,
    const Position *position,
    int ply
) {
    int score;

    if (context != 0 && context->nnue_state_active &&
        context->nnue_states != 0 &&
        ply >= 0 && ply < MAX_SEARCH_PLY &&
        nnue_evaluate_with_state(
            position,
            &context->nnue_states[ply],
            &score
        )) {
#ifdef THETA_VERIFY_NNUE_STATE
        {
            int full_score;
            if (!nnue_evaluate(position, &full_score) ||
                full_score != score) {
                fprintf(stderr, "NNUE state mismatch at ply %d: %d != %d\n",
                        ply, score, full_score);
                abort();
            }
        }
#endif
        return score;
    }
    if (context != 0 && context->classical_state_active &&
        context->classical_states != 0 &&
        ply >= 0 && ply < MAX_SEARCH_PLY &&
        !evaluation_cache_probe(position, &score) &&
        search_materialize_classical_state(context, position, ply)) {
        return evaluate_position_with_state(
            position,
            &context->classical_states[ply],
            0
        );
    }
    if (context != 0 && context->classical_state_active &&
        evaluation_cache_probe(position, &score)) {
        return score;
    }
    return evaluate_position(position);
}

int search_update_nnue_state(
    SearchContext *context,
    const Position *position,
    const Move *move,
    const UndoState *undo,
    int parent_ply
) {
    int updated;

    if (context == 0 || !context->nnue_state_active ||
        context->nnue_states == 0 ||
        parent_ply < 0 || parent_ply + 1 >= MAX_SEARCH_PLY) {
        return 0;
    }
    updated = nnue_state_update(
        position,
        move,
        undo,
        &context->nnue_states[parent_ply],
        &context->nnue_states[parent_ply + 1]
    );
    if (!updated) {
        context->nnue_states[parent_ply + 1].valid = 0;
    }
    return updated;
}

void search_prepare_classical_state(
    SearchContext *context,
    const Move *move,
    const UndoState *undo,
    int child_ply
) {
    if (context == 0 || !context->classical_state_active ||
        context->classical_states == 0 || move == 0 || undo == 0 ||
        child_ply < 0 || child_ply >= MAX_SEARCH_PLY) {
        return;
    }
    context->classical_pending_moves[child_ply] = *move;
    context->classical_pending_undos[child_ply] = *undo;
    context->classical_pending_valid[child_ply] = 1;
    context->classical_states[child_ply].valid = 0;
}

int search_materialize_classical_state(
    SearchContext *context,
    const Position *position,
    int ply
) {
    int updated;

    if (context == 0 || !context->classical_state_active ||
        context->classical_states == 0 || position == 0 ||
        ply < 0 || ply >= MAX_SEARCH_PLY) {
        return 0;
    }

    if (context->classical_pending_valid[ply]) {
        if (ply > 0) {
            updated = classical_eval_state_update(
                position,
                &context->classical_pending_moves[ply],
                &context->classical_pending_undos[ply],
                &context->classical_states[ply - 1],
                &context->classical_states[ply]
            );
        } else {
            updated = classical_eval_state_build(
                position,
                &context->classical_states[ply]
            );
        }
        context->classical_pending_valid[ply] = 0;
        if (!updated) {
            context->classical_states[ply].valid = 0;
            return 0;
        }
    } else if (!context->classical_states[ply].valid ||
               context->classical_states[ply].params_generation !=
                   eval_params_generation()) {
        return classical_eval_state_build(
            position,
            &context->classical_states[ply]
        );
    }

    return context->classical_states[ply].valid;
}

void search_copy_nnue_state(
    SearchContext *context,
    int parent_ply,
    int child_ply
) {
    if (context == 0 || !context->nnue_state_active ||
        context->nnue_states == 0 ||
        parent_ply < 0 || parent_ply >= MAX_SEARCH_PLY ||
        child_ply < 0 || child_ply >= MAX_SEARCH_PLY) {
        return;
    }
    context->nnue_states[child_ply] = context->nnue_states[parent_ply];
}

int search_update_classical_state(
    SearchContext *context,
    const Position *position,
    const Move *move,
    const UndoState *undo,
    int parent_ply
) {
    int updated;

    if (context == 0 || !context->classical_state_active ||
        context->classical_states == 0 ||
        parent_ply < 0 || parent_ply + 1 >= MAX_SEARCH_PLY) {
        return 0;
    }
    updated = classical_eval_state_update(
        position,
        move,
        undo,
        &context->classical_states[parent_ply],
        &context->classical_states[parent_ply + 1]
    );
    if (!updated) {
        context->classical_states[parent_ply + 1].valid = 0;
    }
    return updated;
}

void search_copy_classical_state(
    SearchContext *context,
    int parent_ply,
    int child_ply
) {
    if (context == 0 || !context->classical_state_active ||
        context->classical_states == 0 ||
        parent_ply < 0 || parent_ply >= MAX_SEARCH_PLY ||
        child_ply < 0 || child_ply >= MAX_SEARCH_PLY) {
        return;
    }
    context->classical_states[child_ply] =
        context->classical_states[parent_ply];
    context->classical_pending_valid[child_ply] = 0;
}

int position_has_insufficient_material(const Position *position) {
    int minor_count = 0;
    int bishop_color = -1;
    uint64_t pieces;

    if (position == 0) {
        return 0;
    }

    pieces = position->occupied;
    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        Piece piece = position_piece_at(position, square);
        PieceType type = piece_type(piece);

        pieces &= pieces - 1;
        if (type == PIECE_TYPE_NONE || type == PIECE_TYPE_KING) {
            continue;
        }

        if (type == PIECE_TYPE_PAWN || type == PIECE_TYPE_ROOK ||
            type == PIECE_TYPE_QUEEN) {
            return 0;
        }

        minor_count++;

        if (type == PIECE_TYPE_KNIGHT) {
            bishop_color = -2;
        } else if (bishop_color >= -1) {
            int color = (square_row(square) + square_column(square)) & 1;

            if (bishop_color == -1) {
                bishop_color = color;
            } else if (bishop_color != color) {
                return 0;
            }
        }
    }

    if (minor_count <= 1) {
        return 1;
    }

    return bishop_color >= 0;
}

int position_is_in_check(const Position *position) {
    int king_square = find_king(position, position->side_to_move);

    return is_valid_square(king_square) &&
           is_square_attacked(
               position,
               king_square,
               opposite_color(position->side_to_move)
           );
}

void clear_variation(PrincipalVariation *variation) {
    if (variation != 0) {
        variation->count = 0;
    }
}

void update_variation(
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
