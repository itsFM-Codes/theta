#include "search_state.h"

int initialize_search_shared_state(SearchSharedState *state) {
    return initialize_search_shared_state_mb(
        state,
        DEFAULT_TRANSPOSITION_TABLE_MB
    );
}

int initialize_search_shared_state_mb(SearchSharedState *state, int hash_mb) {
    if (state == 0) {
        return 0;
    }

    return initialize_transposition_table_mb(
        &state->transposition_table,
        hash_mb
    );
}

int resize_search_shared_state(SearchSharedState *state, int hash_mb) {
    TranspositionTable replacement;

    if (state == 0) {
        return 0;
    }

    if (!initialize_transposition_table_mb(&replacement, hash_mb)) {
        return 0;
    }
    destroy_transposition_table(&state->transposition_table);
    state->transposition_table = replacement;
    return 1;
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
