#include <assert.h>

#include "src/chess/zobrist.h"
#include "src/engine/transposition_table.h"

static Move make_test_move(void) {
    Move move;

    move.from = make_square(6, 4);
    move.to = make_square(4, 4);
    move.promotion = PIECE_NONE;
    move.flags = MOVE_FLAG_DOUBLE_PAWN;
    return move;
}

static void test_position_key_restores_after_undo(void) {
    Position position;
    UndoState undo;
    Move move;
    uint64_t starting_key;

    set_starting_position(&position);
    starting_key = position_key(&position);
    move = make_test_move();

    assert(make_move(&position, move, &undo));
    assert(position_key(&position) != starting_key);

    undo_move(&position, move, &undo);
    assert(position_key(&position) == starting_key);
}

static void test_position_key_cache_invalidates_on_position_changes(void) {
    Position position;
    uint64_t white_key;
    uint64_t black_key;

    set_starting_position(&position);
    white_key = position_key(&position);
    assert(position_key(&position) == white_key);

    position.side_to_move = COLOR_BLACK;
    black_key = position_key(&position);
    assert(black_key != white_key);

    position.side_to_move = COLOR_WHITE;
    assert(position_key(&position) == white_key);

    assert(position_set_piece(&position, make_square(6, 0), PIECE_NONE));
    assert(position_key(&position) != white_key);
}

static void test_exact_table_entry(void) {
    TranspositionTable table;
    TranspositionTableStatistics statistics = {};
    Move move = make_test_move();
    Move stored_move;
    int score;

    initialize_transposition_table(&table);
    store_transposition_table(
        &table,
        UINT64_C(12345),
        4,
        87,
        TRANSPOSITION_EXACT,
        move,
        &statistics
    );

    assert(probe_transposition_table(
        &table,
        UINT64_C(12345),
        4,
        -100,
        100,
        &score,
        &stored_move,
        &statistics
    ));
    assert(score == 87);
    assert(stored_move.from == move.from);
    assert(stored_move.to == move.to);
    assert(statistics.probes == 1);
    assert(statistics.key_hits == 1);
    assert(statistics.score_cutoffs == 1);
    assert(statistics.stores == 1);
    assert(transposition_table_hashfull(&table) == 0);

    destroy_transposition_table(&table);
}

static void test_bound_table_entry(void) {
    TranspositionTable table;
    TranspositionTableStatistics statistics = {};
    Move move = make_test_move();
    int score;

    initialize_transposition_table(&table);
    store_transposition_table(
        &table,
        UINT64_C(67890),
        3,
        50,
        TRANSPOSITION_LOWER_BOUND,
        move,
        &statistics
    );

    assert(probe_transposition_table(
        &table,
        UINT64_C(67890),
        3,
        -100,
        40,
        &score,
        0,
        &statistics
    ));
    assert(score == 50);
    assert(statistics.probes == 1);
    assert(statistics.key_hits == 1);
    assert(statistics.score_cutoffs == 1);

    destroy_transposition_table(&table);
}

static void test_table_move_survives_unusable_score(void) {
    TranspositionTable table;
    TranspositionTableStatistics statistics = {};
    Move move = make_test_move();
    Move stored_move;
    int score = 0;

    initialize_transposition_table(&table);
    store_transposition_table(
        &table,
        UINT64_C(13579),
        1,
        25,
        TRANSPOSITION_EXACT,
        move,
        &statistics
    );

    assert(!probe_transposition_table(
        &table,
        UINT64_C(13579),
        4,
        -100,
        100,
        &score,
        &stored_move,
        &statistics
    ));
    assert(stored_move.from == move.from);
    assert(stored_move.to == move.to);
    assert(statistics.probes == 1);
    assert(statistics.key_hits == 1);
    assert(statistics.score_cutoffs == 0);

    destroy_transposition_table(&table);
}

static void test_static_evaluation_entry(void) {
    TranspositionTable table;
    TranspositionTableStatistics statistics = {};
    Move move = make_test_move();
    int static_evaluation = 0;

    initialize_transposition_table(&table);
    store_transposition_table_with_static_evaluation(
        &table,
        UINT64_C(24680),
        3,
        10,
        TRANSPOSITION_EXACT,
        move,
        123,
        &statistics
    );

    assert(probe_transposition_static_evaluation(
        &table,
        UINT64_C(24680),
        &static_evaluation
    ));
    assert(static_evaluation == 123);

    store_transposition_table(
        &table,
        UINT64_C(24680),
        4,
        12,
        TRANSPOSITION_EXACT,
        move,
        &statistics
    );
    static_evaluation = 0;
    assert(probe_transposition_static_evaluation(
        &table,
        UINT64_C(24680),
        &static_evaluation
    ));
    assert(static_evaluation == 123);

    destroy_transposition_table(&table);
}

static void test_configurable_clustered_table(void) {
    TranspositionTable table;

    assert(initialize_transposition_table_mb(&table, 1));
    assert(table.size_mb == 1);
    assert(table.count >= TRANSPOSITION_CLUSTER_SIZE);
    assert(table.count % TRANSPOSITION_CLUSTER_SIZE == 0);
    assert(table.bucket_count == table.count / TRANSPOSITION_CLUSTER_SIZE);
    assert(table.lock_count == TRANSPOSITION_LOCK_COUNT);
    destroy_transposition_table(&table);
}

int main(void) {
    test_position_key_restores_after_undo();
    test_position_key_cache_invalidates_on_position_changes();
    test_exact_table_entry();
    test_bound_table_entry();
    test_table_move_survives_unusable_score();
    test_static_evaluation_entry();
    test_configurable_clustered_table();
    return 0;
}
