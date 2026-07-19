#ifndef SEARCH_H
#define SEARCH_H

#include <atomic>

#include "src/chess/move.h"
#include "src/chess/position.h"
#include "search_state.h"

#define SEARCH_INFINITY 30000
#define SEARCH_CHECKMATE 29000
#define MAX_PRINCIPAL_VARIATION 64
#define DEFAULT_SEARCH_POLL_INTERVAL 64

typedef struct PrincipalVariation {
    Move moves[MAX_PRINCIPAL_VARIATION];
    int count;
} PrincipalVariation;

typedef struct SearchStatistics {
    uint64_t nodes;
    uint64_t quiescence_nodes;
    uint64_t transposition_probes;
    uint64_t transposition_key_hits;
    uint64_t transposition_cutoffs;
    uint64_t transposition_stores;
    uint64_t beta_cutoffs;
    uint64_t first_move_beta_cutoffs;
    uint64_t null_move_attempts;
    uint64_t null_move_cutoffs;
    uint64_t reverse_futility_prunes;
    uint64_t late_move_prunes;
    uint64_t static_futility_prunes;
    uint64_t razoring_prunes;
    uint64_t delta_prunes;
    uint64_t see_prunes;
    uint64_t quiescence_check_moves;
    uint64_t late_move_reductions;
    uint64_t late_move_researches;
    uint64_t aspiration_failures;
    uint64_t aspiration_researches;
    int selective_depth;
    int elapsed_ms;
    int hashfull;
} SearchStatistics;

typedef void (*SearchInfoCallback)(
    int depth,
    int score,
    const PrincipalVariation *variation,
    const SearchStatistics *statistics,
    void *user_data
);

typedef struct SearchLimits {
    int soft_time_ms;
    int hard_time_ms;
    uint64_t node_limit;
    uint64_t poll_interval;
    std::atomic<bool> *stop_requested;
    const uint64_t *game_history;
    int game_history_count;
} SearchLimits;

int search_position(Position *position, int depth, Move *best_move);
int search_position_with_state(
    SearchSharedState *shared_state,
    Position *position,
    int depth,
    Move *best_move
);
int search_iterative(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth
);
int search_iterative_with_callback(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
);
int search_iterative_with_callback_and_node_limit(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    uint64_t node_limit,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
);
int search_iterative_with_limits(
    Position *position,
    int maximum_depth,
    const SearchLimits *limits,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
);
int search_iterative_with_state_and_limits(
    SearchSharedState *shared_state,
    Position *position,
    int maximum_depth,
    const SearchLimits *limits,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
);

#endif // SEARCH_H
