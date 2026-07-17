#ifndef SEARCH_INTERNAL_H
#define SEARCH_INTERNAL_H

#include <time.h>

#include "search.h"

#define MAX_KILLER_PLY 64

typedef struct SearchContext {
    clock_t start_time;
    int time_limit_ms;
    int stopped;
    Move killer_moves[MAX_KILLER_PLY][2];
    int history[2][SQUARE_COUNT][SQUARE_COUNT];
} SearchContext;

void initialize_search_context(SearchContext *context, int time_limit_ms);
int search_has_stopped(SearchContext *context);

int position_is_in_check(const Position *position);

void clear_variation(PrincipalVariation *variation);
void update_variation(
    PrincipalVariation *variation,
    Move move,
    const PrincipalVariation *child_variation
);

#endif // SEARCH_INTERNAL_H
