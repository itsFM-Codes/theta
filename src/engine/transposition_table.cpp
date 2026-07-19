#include "transposition_table.h"

#include <stdlib.h>
#include <string.h>

void initialize_transposition_table(TranspositionTable *table) {
    if (table == 0) {
        return;
    }

    table->entries = (TranspositionEntry *)calloc(
        TRANSPOSITION_TABLE_SIZE,
        sizeof(TranspositionEntry)
    );
    table->count = table->entries == 0 ? 0 : TRANSPOSITION_TABLE_SIZE;
}

void clear_transposition_table(TranspositionTable *table) {
    if (table == 0) {
        return;
    }

    if (table->entries != 0 && table->count > 0) {
        memset(
            table->entries,
            0,
            (size_t)table->count * sizeof(TranspositionEntry)
        );
    }
}

void destroy_transposition_table(TranspositionTable *table) {
    if (table == 0) {
        return;
    }

    free(table->entries);
    table->entries = 0;
    table->count = 0;
}

int probe_transposition_table(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int alpha,
    int beta,
    int *score,
    Move *best_move,
    TranspositionTableStatistics *statistics
) {
    const TranspositionEntry *entry;

    if (table == 0 || table->entries == 0 || table->count == 0) {
        return 0;
    }

    if (statistics != 0) {
        statistics->probes++;
    }

    entry = &table->entries[key % (uint64_t)table->count];

    if (!entry->is_valid || entry->key != key) {
        return 0;
    }

    if (statistics != 0) {
        statistics->key_hits++;
    }

    if (best_move != 0) {
        *best_move = entry->best_move;
    }

    if (entry->depth < depth) {
        return 0;
    }

    if (entry->flag == TRANSPOSITION_EXACT ||
        (entry->flag == TRANSPOSITION_LOWER_BOUND && entry->score >= beta) ||
        (entry->flag == TRANSPOSITION_UPPER_BOUND && entry->score <= alpha)) {
        if (score != 0) {
            *score = entry->score;
        }

        if (statistics != 0) {
            statistics->score_cutoffs++;
        }

        return 1;
    }

    return 0;
}

int probe_transposition_static_evaluation(
    const TranspositionTable *table,
    uint64_t key,
    int *static_evaluation
) {
    const TranspositionEntry *entry;

    if (table == 0 || table->entries == 0 || table->count == 0) {
        return 0;
    }

    entry = &table->entries[key % (uint64_t)table->count];
    if (!entry->is_valid || entry->key != key ||
        !entry->has_static_evaluation) {
        return 0;
    }

    if (static_evaluation != 0) {
        *static_evaluation = entry->static_evaluation;
    }

    return 1;
}

static void store_transposition_table_with_optional_static_evaluation(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int score,
    TranspositionFlag flag,
    Move best_move,
    int has_static_evaluation,
    int static_evaluation,
    TranspositionTableStatistics *statistics
) {
    TranspositionEntry *entry;
    int preserve_static_evaluation = 0;
    int preserved_static_evaluation = 0;

    if (table == 0 || table->entries == 0 || table->count == 0) {
        return;
    }

    entry = &table->entries[key % (uint64_t)table->count];

    if (entry->is_valid && entry->key == key && entry->depth > depth) {
        if (has_static_evaluation && !entry->has_static_evaluation) {
            entry->static_evaluation = static_evaluation;
            entry->has_static_evaluation = 1;
        }
        return;
    }

    if (entry->is_valid && entry->key == key &&
        entry->has_static_evaluation && !has_static_evaluation) {
        preserve_static_evaluation = 1;
        preserved_static_evaluation = entry->static_evaluation;
    }

    entry->key = key;
    entry->best_move = best_move;
    entry->score = score;
    entry->static_evaluation = has_static_evaluation
        ? static_evaluation
        : preserved_static_evaluation;
    entry->depth = depth;
    entry->flag = flag;
    entry->is_valid = 1;
    entry->has_static_evaluation =
        has_static_evaluation || preserve_static_evaluation;
    if (statistics != 0) {
        statistics->stores++;
    }
}

void store_transposition_table(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int score,
    TranspositionFlag flag,
    Move best_move,
    TranspositionTableStatistics *statistics
) {
    store_transposition_table_with_optional_static_evaluation(
        table,
        key,
        depth,
        score,
        flag,
        best_move,
        0,
        0,
        statistics
    );
}

void store_transposition_table_with_static_evaluation(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int score,
    TranspositionFlag flag,
    Move best_move,
    int static_evaluation,
    TranspositionTableStatistics *statistics
) {
    store_transposition_table_with_optional_static_evaluation(
        table,
        key,
        depth,
        score,
        flag,
        best_move,
        1,
        static_evaluation,
        statistics
    );
}

int transposition_table_hashfull(const TranspositionTable *table) {
    int index;
    int occupied = 0;

    if (table == 0 || table->entries == 0 || table->count == 0) {
        return 0;
    }

    for (index = 0; index < table->count; ++index) {
        if (table->entries[index].is_valid) {
            occupied++;
        }
    }

    return occupied * 1000 / table->count;
}
