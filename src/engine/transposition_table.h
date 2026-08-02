#ifndef TRANSPOSITION_TABLE_H
#define TRANSPOSITION_TABLE_H

#include <stdint.h>
// Locks are disabled while search is single-threaded.
class TranspositionMutex {
public:
    void lock() {}
    void unlock() {}
};

#include "src/chess/move.h"

#define DEFAULT_TRANSPOSITION_TABLE_MB 16
#define TRANSPOSITION_CLUSTER_SIZE 4
#define TRANSPOSITION_LOCK_COUNT 256

typedef enum TranspositionFlag {
    TRANSPOSITION_EXACT,
    TRANSPOSITION_LOWER_BOUND,
    TRANSPOSITION_UPPER_BOUND
} TranspositionFlag;

typedef struct TranspositionEntry {
    uint64_t key;
    Move best_move;
    int16_t score;
    int16_t static_evaluation;
    int16_t depth;
    uint8_t flag;
    uint8_t is_valid;
    uint8_t has_static_evaluation;
    uint8_t generation;
} TranspositionEntry;

typedef struct TranspositionTable {
    TranspositionEntry *entries;
    int count;
    int bucket_count;
    int size_mb;
    uint8_t generation;
    TranspositionMutex *locks;
    int lock_count;
} TranspositionTable;

typedef struct TranspositionTableStatistics {
    uint64_t probes;
    uint64_t key_hits;
    uint64_t score_cutoffs;
    uint64_t stores;
} TranspositionTableStatistics;

void initialize_transposition_table(TranspositionTable *table);
int initialize_transposition_table_mb(TranspositionTable *table, int size_mb);
void clear_transposition_table(TranspositionTable *table);
void destroy_transposition_table(TranspositionTable *table);
void advance_transposition_table_generation(TranspositionTable *table);

int probe_transposition_table(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int alpha,
    int beta,
    int *score,
    Move *best_move,
    TranspositionTableStatistics *statistics
);

int transposition_table_hashfull(const TranspositionTable *table);

int probe_transposition_static_evaluation(
    const TranspositionTable *table,
    uint64_t key,
    int *static_evaluation
);
int probe_transposition_entry(
    const TranspositionTable *table,
    uint64_t key,
    TranspositionEntry *entry
);

void store_transposition_table(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int score,
    TranspositionFlag flag,
    Move best_move,
    TranspositionTableStatistics *statistics
);

void store_transposition_table_with_static_evaluation(
    TranspositionTable *table,
    uint64_t key,
    int depth,
    int score,
    TranspositionFlag flag,
    Move best_move,
    int static_evaluation,
    TranspositionTableStatistics *statistics
);

#endif // TRANSPOSITION_TABLE_H
