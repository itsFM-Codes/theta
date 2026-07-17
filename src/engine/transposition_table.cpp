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

void destroy_transposition_table(TranspositionTable *table) {
    if (table == 0) {
        return;
    }

    free(table->entries);
    table->entries = 0;
    table->count = 0;
}

int probe_transposition_table(
    const TranspositionTable *table,
    uint64_t key,
    int depth,
    int alpha,
    int beta,
    int *score,
    Move *best_move
) {
    const TranspositionEntry *entry;

    if (table == 0 || table->entries == 0 || table->count == 0) {
        return 0;
    }

    entry = &table->entries[key % (uint64_t)table->count];

    if (!entry->is_valid || entry->key != key) {
        return 0;
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

        return 1;
    }

    return 0;
}

void store_transposition_table(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int score,
    TranspositionFlag flag,
    Move best_move
) {
    TranspositionEntry *entry;

    if (table == 0 || table->entries == 0 || table->count == 0) {
        return;
    }

    entry = &table->entries[key % (uint64_t)table->count];

    if (entry->is_valid && entry->key == key && entry->depth > depth) {
        return;
    }

    entry->key = key;
    entry->best_move = best_move;
    entry->score = score;
    entry->depth = depth;
    entry->flag = flag;
    entry->is_valid = 1;
}
