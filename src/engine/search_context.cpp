#include "search_internal.h"

#include <string.h>

#include "src/chess/movegen.h"
#include "src/chess/zobrist.h"

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
    context->selective_depth = 0;
    context->position_key_count = 0;
    memset(context->history, 0, sizeof(context->history));
    memset(context->continuation_history, 0,
           sizeof(context->continuation_history));
    memset(context->capture_history, 0, sizeof(context->capture_history));
    memset(context->static_evaluation_valid, 0,
           sizeof(context->static_evaluation_valid));
    context->shared_state = shared_state;
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
        context->static_evaluations[ply] = 0;
    }
}

void destroy_search_context(SearchContext *context) {
    // SearchContext borrows shared resources and owns no dynamic memory.
    (void)context;
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
    statistics->selective_depth = context == 0 ? 0 : context->selective_depth;
    statistics->elapsed_ms = search_elapsed_ms(context);
    statistics->hashfull = context == 0
        ? 0
        : transposition_table_hashfull(table);
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

int position_has_insufficient_material(const Position *position) {
    int minor_count = 0;
    int bishop_color = -1;
    int square;

    if (position == 0) {
        return 0;
    }

    for (square = 0; square < SQUARE_COUNT; ++square) {
        Piece piece = position_piece_at(position, square);
        PieceType type = piece_type(piece);

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
