#ifndef TRANSPOSITION_TABLE_H
#define TRANSPOSITION_TABLE_H

#include <stdint.h>

#include "src/chess/move.h"

#define TRANSPOSITION_TABLE_SIZE (1 << 18)

typedef enum TranspositionFlag {
    TRANSPOSITION_EXACT,
    TRANSPOSITION_LOWER_BOUND,
    TRANSPOSITION_UPPER_BOUND
} TranspositionFlag;

typedef struct TranspositionEntry {
    uint64_t key;
    Move best_move;
    int score;
    int depth;
    int flag;
    int is_valid;
} TranspositionEntry;

typedef struct TranspositionTable {
    TranspositionEntry *entries;
    int count;
} TranspositionTable;

void initialize_transposition_table(TranspositionTable *table);
void destroy_transposition_table(TranspositionTable *table);

int probe_transposition_table(
    const TranspositionTable *table,
    uint64_t key,
    int depth,
    int alpha,
    int beta,
    int *score,
    Move *best_move
);

void store_transposition_table(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int score,
    TranspositionFlag flag,
    Move best_move
);

#endif // TRANSPOSITION_TABLE_H
