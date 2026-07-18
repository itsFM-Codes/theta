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
    int static_evaluation;
    int depth;
    int flag;
    int is_valid;
    int has_static_evaluation;
} TranspositionEntry;

typedef struct TranspositionTable {
    TranspositionEntry *entries;
    int count;
    uint64_t probes;
    uint64_t key_hits;
    uint64_t score_cutoffs;
    uint64_t stores;
} TranspositionTable;

void initialize_transposition_table(TranspositionTable *table);
void destroy_transposition_table(TranspositionTable *table);

int probe_transposition_table(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int alpha,
    int beta,
    int *score,
    Move *best_move
);

int transposition_table_hashfull(const TranspositionTable *table);

int probe_transposition_static_evaluation(
    const TranspositionTable *table,
    uint64_t key,
    int *static_evaluation
);

void store_transposition_table(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int score,
    TranspositionFlag flag,
    Move best_move
);

void store_transposition_table_with_static_evaluation(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int score,
    TranspositionFlag flag,
    Move best_move,
    int static_evaluation
);

#endif // TRANSPOSITION_TABLE_H
