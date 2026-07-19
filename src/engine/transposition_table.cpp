#include "transposition_table.h"

#include <new>
#include <stdlib.h>
#include <string.h>

class TranspositionLock {
public:
    explicit TranspositionLock(TranspositionMutex &mutex) : mutex_(mutex) {
        mutex_.lock();
    }
    ~TranspositionLock() { mutex_.unlock(); }
private:
    TranspositionMutex &mutex_;
};

static int table_lock_index(const TranspositionTable *table, uint64_t bucket) {
    return table->lock_count == 0
        ? 0
        : (int)(bucket % (uint64_t)table->lock_count);
}

void initialize_transposition_table(TranspositionTable *table) {
    initialize_transposition_table_mb(table, DEFAULT_TRANSPOSITION_TABLE_MB);
}

int initialize_transposition_table_mb(TranspositionTable *table, int size_mb) {
    size_t bytes;
    size_t entry_count;

    if (table == 0 || size_mb < 1) {
        return 0;
    }

    table->entries = 0;
    table->count = 0;
    table->bucket_count = 0;
    table->size_mb = 0;
    table->generation = 0;
    table->locks = 0;
    table->lock_count = 0;

    bytes = (size_t)size_mb * 1024u * 1024u;
    entry_count = bytes / sizeof(TranspositionEntry);
    entry_count -= entry_count % TRANSPOSITION_CLUSTER_SIZE;
    if (entry_count < TRANSPOSITION_CLUSTER_SIZE || entry_count > INT32_MAX) {
        return 0;
    }

    table->entries = (TranspositionEntry *)calloc(
        entry_count,
        sizeof(TranspositionEntry)
    );
    if (table->entries == 0) {
        return 0;
    }

    table->locks = new (std::nothrow)
        TranspositionMutex[TRANSPOSITION_LOCK_COUNT];
    if (table->locks == 0) {
        free(table->entries);
        table->entries = 0;
        return 0;
    }

    table->count = (int)entry_count;
    table->bucket_count = table->count / TRANSPOSITION_CLUSTER_SIZE;
    table->size_mb = size_mb;
    table->lock_count = TRANSPOSITION_LOCK_COUNT;
    return 1;
}

void clear_transposition_table(TranspositionTable *table) {
    if (table == 0 || table->entries == 0) {
        return;
    }

    memset(
        table->entries,
        0,
        (size_t)table->count * sizeof(TranspositionEntry)
    );
    table->generation = 0;
}

void destroy_transposition_table(TranspositionTable *table) {
    if (table == 0) {
        return;
    }

    delete[] table->locks;
    free(table->entries);
    table->entries = 0;
    table->locks = 0;
    table->count = 0;
    table->bucket_count = 0;
    table->size_mb = 0;
    table->lock_count = 0;
}

void advance_transposition_table_generation(TranspositionTable *table) {
    if (table != 0) {
        table->generation++;
    }
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
    int bucket_start;
    int index;

    if (table == 0 || table->entries == 0 || table->bucket_count == 0) {
        return 0;
    }

    if (statistics != 0) {
        statistics->probes++;
    }

    bucket_start = (int)(key % (uint64_t)table->bucket_count) *
        TRANSPOSITION_CLUSTER_SIZE;
    TranspositionLock lock(table->locks[table_lock_index(
        table, (uint64_t)(bucket_start / TRANSPOSITION_CLUSTER_SIZE)
    )]);

    for (index = 0; index < TRANSPOSITION_CLUSTER_SIZE; ++index) {
        const TranspositionEntry *entry = &table->entries[bucket_start + index];

        if (!entry->is_valid || entry->key != key) {
            continue;
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
            (entry->flag == TRANSPOSITION_LOWER_BOUND &&
             entry->score >= beta) ||
            (entry->flag == TRANSPOSITION_UPPER_BOUND &&
             entry->score <= alpha)) {
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

    return 0;
}

int probe_transposition_static_evaluation(
    const TranspositionTable *table,
    uint64_t key,
    int *static_evaluation
) {
    int bucket_start;
    int index;

    if (table == 0 || table->entries == 0 || table->bucket_count == 0) {
        return 0;
    }

    bucket_start = (int)(key % (uint64_t)table->bucket_count) *
        TRANSPOSITION_CLUSTER_SIZE;
    TranspositionLock lock(table->locks[table_lock_index(
        table, (uint64_t)(bucket_start / TRANSPOSITION_CLUSTER_SIZE)
    )]);

    for (index = 0; index < TRANSPOSITION_CLUSTER_SIZE; ++index) {
        const TranspositionEntry *entry = &table->entries[bucket_start + index];
        if (entry->is_valid && entry->key == key &&
            entry->has_static_evaluation) {
            if (static_evaluation != 0) {
                *static_evaluation = entry->static_evaluation;
            }
            return 1;
        }
    }
    return 0;
}

int probe_transposition_entry(
    const TranspositionTable *table,
    uint64_t key,
    TranspositionEntry *result
) {
    int bucket_start;
    int index;

    if (table == 0 || table->entries == 0 || result == 0) {
        return 0;
    }

    bucket_start = (int)(key % (uint64_t)table->bucket_count) *
        TRANSPOSITION_CLUSTER_SIZE;
    TranspositionLock lock(table->locks[table_lock_index(
        table, (uint64_t)(bucket_start / TRANSPOSITION_CLUSTER_SIZE)
    )]);
    for (index = 0; index < TRANSPOSITION_CLUSTER_SIZE; ++index) {
        const TranspositionEntry *entry = &table->entries[bucket_start + index];
        if (entry->is_valid && entry->key == key) {
            *result = *entry;
            return 1;
        }
    }
    return 0;
}

static void store_transposition_table_internal(
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
    int bucket_start;
    int index;
    TranspositionEntry *replacement = 0;
    int replacement_priority = INT32_MAX;

    if (table == 0 || table->entries == 0 || table->bucket_count == 0) {
        return;
    }

    bucket_start = (int)(key % (uint64_t)table->bucket_count) *
        TRANSPOSITION_CLUSTER_SIZE;
    TranspositionLock lock(table->locks[table_lock_index(
        table, (uint64_t)(bucket_start / TRANSPOSITION_CLUSTER_SIZE)
    )]);

    for (index = 0; index < TRANSPOSITION_CLUSTER_SIZE; ++index) {
        TranspositionEntry *entry = &table->entries[bucket_start + index];
        int priority;

        if (entry->is_valid && entry->key == key) {
            replacement = entry;
            break;
        }
        if (!entry->is_valid) {
            replacement = entry;
            break;
        }

        priority = entry->depth +
            (entry->generation == table->generation ? 8 : 0) +
            (entry->flag == TRANSPOSITION_EXACT ? 2 : 0);
        if (priority < replacement_priority) {
            replacement_priority = priority;
            replacement = entry;
        }
    }

    if (replacement->is_valid && replacement->key == key &&
        replacement->depth > depth && flag != TRANSPOSITION_EXACT) {
        if (has_static_evaluation && !replacement->has_static_evaluation) {
            replacement->static_evaluation = static_evaluation;
            replacement->has_static_evaluation = 1;
        }
        return;
    }

    if (!has_static_evaluation && replacement->is_valid &&
        replacement->key == key && replacement->has_static_evaluation) {
        static_evaluation = replacement->static_evaluation;
        has_static_evaluation = 1;
    }

    replacement->key = key;
    replacement->best_move = best_move;
    replacement->score = score;
    replacement->static_evaluation = static_evaluation;
    replacement->depth = depth;
    replacement->flag = flag;
    replacement->is_valid = 1;
    replacement->has_static_evaluation = has_static_evaluation;
    replacement->generation = table->generation;
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
    store_transposition_table_internal(
        table, key, depth, score, flag, best_move, 0, 0, statistics
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
    store_transposition_table_internal(
        table, key, depth, score, flag, best_move, 1, static_evaluation,
        statistics
    );
}

int transposition_table_hashfull(const TranspositionTable *table) {
    int sample_count;
    int occupied = 0;
    int index;

    if (table == 0 || table->entries == 0 || table->count == 0) {
        return 0;
    }

    sample_count = table->count < 1000 ? table->count : 1000;
    for (index = 0; index < sample_count; ++index) {
        uint64_t bucket = (uint64_t)(index / TRANSPOSITION_CLUSTER_SIZE);
        TranspositionLock lock(
            table->locks[table_lock_index(table, bucket)]
        );
        if (table->entries[index].is_valid &&
            table->entries[index].generation == table->generation) {
            occupied++;
        }
    }

    return occupied * 1000 / sample_count;
}
