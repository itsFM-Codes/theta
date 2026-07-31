#ifndef SEARCH_STATE_H
#define SEARCH_STATE_H

#include "transposition_table.h"

typedef struct SearchHeuristicTables {
    int history[2][SQUARE_COUNT][SQUARE_COUNT];
    short continuation_history[PIECE_TYPE_KING + 1][SQUARE_COUNT]
        [PIECE_TYPE_KING + 1][SQUARE_COUNT];
    int capture_history[2][PIECE_TYPE_KING + 1][SQUARE_COUNT]
        [PIECE_TYPE_KING + 1];
} SearchHeuristicTables;

// State shared by one engine instance.
// Make table access thread-safe before parallel search.
typedef struct SearchSharedState {
    TranspositionTable transposition_table;
    SearchHeuristicTables *heuristics;
} SearchSharedState;

int initialize_search_shared_state(SearchSharedState *state);
int initialize_search_shared_state_mb(SearchSharedState *state, int hash_mb);
int resize_search_shared_state(SearchSharedState *state, int hash_mb);
void clear_search_shared_state(SearchSharedState *state);
void destroy_search_shared_state(SearchSharedState *state);

#endif // SEARCH_STATE_H
