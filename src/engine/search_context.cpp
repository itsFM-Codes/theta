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

void initialize_search_context(SearchContext *context, int time_limit_ms) {
    int ply;

    context->start_time = std::chrono::steady_clock::now();
    context->time_limit_ms = time_limit_ms;
    context->stopped = 0;
    context->nodes = 0;
    context->node_limit = 0;
    context->quiescence_nodes = 0;
    context->selective_depth = 0;
    context->position_key_count = 0;
    memset(context->history, 0, sizeof(context->history));
    initialize_transposition_table(&context->table);

    for (ply = 0; ply < MAX_KILLER_PLY; ++ply) {
        clear_move(&context->killer_moves[ply][0]);
        clear_move(&context->killer_moves[ply][1]);
    }
}

void destroy_search_context(SearchContext *context) {
    if (context != 0) {
        destroy_transposition_table(&context->table);
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

    if (context->time_limit_ms <= 0) {
        return 0;
    }

    elapsed = search_elapsed_ms(context);

    if (elapsed >= context->time_limit_ms) {
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
    if (context->node_limit > 0 && context->nodes > context->node_limit) {
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

void search_get_statistics(
    const SearchContext *context,
    SearchStatistics *statistics
) {
    if (statistics == 0) {
        return;
    }

    statistics->nodes = context == 0 ? 0 : context->nodes;
    statistics->quiescence_nodes = context == 0 ? 0 : context->quiescence_nodes;
    statistics->transposition_probes = context == 0 ? 0 : context->table.probes;
    statistics->transposition_key_hits = context == 0 ? 0 : context->table.key_hits;
    statistics->transposition_cutoffs = context == 0
        ? 0
        : context->table.score_cutoffs;
    statistics->transposition_stores = context == 0 ? 0 : context->table.stores;
    statistics->selective_depth = context == 0 ? 0 : context->selective_depth;
    statistics->elapsed_ms = search_elapsed_ms(context);
    statistics->hashfull = context == 0
        ? 0
        : transposition_table_hashfull(&context->table);
}

int search_push_position(SearchContext *context, const Position *position) {
    if (context == 0 || position == 0 ||
        context->position_key_count >= MAX_SEARCH_PLY) {
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

    for (index = 0; index < context->position_key_count - 1; ++index) {
        if (context->position_keys[index] == key) {
            ++matches;
        }
    }

    return matches >= 2;
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
