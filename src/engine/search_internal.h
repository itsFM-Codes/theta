#ifndef SEARCH_INTERNAL_H
#define SEARCH_INTERNAL_H

#include <chrono>

#include "search.h"
#include "transposition_table.h"

#define MAX_KILLER_PLY 64
#define MAX_SEARCH_PLY 128

typedef struct SearchContext {
    std::chrono::steady_clock::time_point start_time;
    int time_limit_ms;
    int stopped;
    uint64_t nodes;
    uint64_t quiescence_nodes;
    int selective_depth;
    Move killer_moves[MAX_KILLER_PLY][2];
    int history[2][SQUARE_COUNT][SQUARE_COUNT];
    uint64_t position_keys[MAX_SEARCH_PLY];
    int position_key_count;
    TranspositionTable table;
} SearchContext;

void initialize_search_context(SearchContext *context, int time_limit_ms);
void destroy_search_context(SearchContext *context);
int search_has_stopped(SearchContext *context);
int search_elapsed_ms(const SearchContext *context);
void search_record_node(SearchContext *context, int ply, int is_quiescence);
void search_get_statistics(
    const SearchContext *context,
    SearchStatistics *statistics
);
int search_push_position(SearchContext *context, const Position *position);
void search_pop_position(SearchContext *context);
int search_is_draw(const SearchContext *context, const Position *position);
int position_has_insufficient_material(const Position *position);

int position_is_in_check(const Position *position);

void clear_variation(PrincipalVariation *variation);
void update_variation(
    PrincipalVariation *variation,
    Move move,
    const PrincipalVariation *child_variation
);

#endif // SEARCH_INTERNAL_H
