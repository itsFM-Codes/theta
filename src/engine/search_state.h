#ifndef SEARCH_STATE_H
#define SEARCH_STATE_H

#include "transposition_table.h"

// State shared by one engine instance.
// Make table access thread-safe before parallel search.
typedef struct SearchSharedState {
    TranspositionTable transposition_table;
} SearchSharedState;

int initialize_search_shared_state(SearchSharedState *state);
int initialize_search_shared_state_mb(SearchSharedState *state, int hash_mb);
int resize_search_shared_state(SearchSharedState *state, int hash_mb);
void clear_search_shared_state(SearchSharedState *state);
void destroy_search_shared_state(SearchSharedState *state);

#endif // SEARCH_STATE_H
