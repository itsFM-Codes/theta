#ifndef SEARCH_STATE_H
#define SEARCH_STATE_H

#include "transposition_table.h"

// Resources shared by searches belonging to one engine instance. Future search
// workers should borrow this state while keeping their SearchContext private.
// Transposition-table entry access must be made thread-safe before more than
// one worker uses the state concurrently.
typedef struct SearchSharedState {
    TranspositionTable transposition_table;
} SearchSharedState;

int initialize_search_shared_state(SearchSharedState *state);
void clear_search_shared_state(SearchSharedState *state);
void destroy_search_shared_state(SearchSharedState *state);

#endif // SEARCH_STATE_H
