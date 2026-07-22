#ifndef SEARCH_INTERNAL_H
#define SEARCH_INTERNAL_H

#include <chrono>

#include "search.h"
#include "search_state.h"
#include "transposition_table.h"

#define MAX_KILLER_PLY 64
#define MAX_SEARCH_PLY 128
#define MAX_POSITION_HISTORY 2048

typedef struct SearchContext {
    std::chrono::steady_clock::time_point start_time;
    int hard_time_limit_ms;
    int stopped;
    std::atomic<bool> *stop_requested;
    uint64_t nodes;
    uint64_t node_limit;
    uint64_t poll_interval;
    uint64_t next_poll_node;
    uint64_t quiescence_nodes;
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
    uint64_t probcut_attempts;
    uint64_t probcut_cutoffs;
    uint64_t singular_attempts;
    uint64_t singular_extensions;
    int selective_depth;
    Move killer_moves[MAX_KILLER_PLY][2];
    Move counter_moves[SQUARE_COUNT][SQUARE_COUNT];
    Move line_moves[MAX_SEARCH_PLY];
    int history[2][SQUARE_COUNT][SQUARE_COUNT];
    // Previous-piece and reply-piece history.
    short continuation_history[PIECE_TYPE_KING + 1][PIECE_TYPE_KING + 1]
        [SQUARE_COUNT];
    int capture_history[2][PIECE_TYPE_KING + 1][SQUARE_COUNT][PIECE_TYPE_KING + 1];
    int static_evaluations[MAX_SEARCH_PLY];
    int static_evaluation_valid[MAX_SEARCH_PLY];
    uint64_t position_keys[MAX_POSITION_HISTORY];
    int position_key_count;
    int draw_score;
    // Shared engine state; the rest is worker-local.
    SearchSharedState *shared_state;
    TranspositionTableStatistics transposition_statistics;
} SearchContext;

void initialize_search_context(
    SearchContext *context,
    SearchSharedState *shared_state,
    int time_limit_ms
);
void destroy_search_context(SearchContext *context);
int search_has_stopped(SearchContext *context);
int search_elapsed_ms(const SearchContext *context);
void search_record_node(SearchContext *context, int ply, int is_quiescence);
void search_set_node_limit(SearchContext *context, uint64_t node_limit);
void search_set_limits(SearchContext *context, const SearchLimits *limits);
void search_set_position_history(
    SearchContext *context,
    const uint64_t *keys,
    int count
);
void search_get_statistics(
    const SearchContext *context,
    SearchStatistics *statistics
);
int search_push_position(SearchContext *context, const Position *position);
void search_pop_position(SearchContext *context);
int search_is_draw(const SearchContext *context, const Position *position);
int search_draw_score(const SearchContext *context);
int position_has_insufficient_material(const Position *position);

int position_is_in_check(const Position *position);
int search_score_to_table(int score, int ply);
int search_score_from_table(int score, int ply);
int probe_search_transposition_table(
    SearchContext *context,
    uint64_t key,
    int depth,
    int alpha,
    int beta,
    int ply,
    int *score,
    Move *best_move
);

void clear_variation(PrincipalVariation *variation);
void update_variation(
    PrincipalVariation *variation,
    Move move,
    const PrincipalVariation *child_variation
);

#endif // SEARCH_INTERNAL_H
