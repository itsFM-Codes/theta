#include "search_state.h"

int initialize_search_shared_state(SearchSharedState *state) {
    if (state == 0) {
        return 0;
    }

    initialize_transposition_table(&state->transposition_table);
    return state->transposition_table.entries != 0;
}

void clear_search_shared_state(SearchSharedState *state) {
    if (state != 0) {
        clear_transposition_table(&state->transposition_table);
    }
}

void destroy_search_shared_state(SearchSharedState *state) {
    if (state != 0) {
        destroy_transposition_table(&state->transposition_table);
    }
}
