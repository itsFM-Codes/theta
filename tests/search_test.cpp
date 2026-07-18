#include <assert.h>

#include "src/chess/board.h"
#include "src/chess/fen.h"
#include "src/chess/movegen.h"
#include "src/chess/position.h"
#include "src/engine/search.h"
#include "src/engine/search_internal.h"
#include "src/eval/evaluation.h"

static SearchStatistics callback_statistics;
static int callback_invocations;

static void capture_search_statistics(
    int depth,
    int score,
    const PrincipalVariation *variation,
    const SearchStatistics *statistics,
    void *user_data
) {
    (void)depth;
    (void)score;
    (void)variation;
    (void)user_data;
    callback_statistics = *statistics;
    callback_invocations++;
}

static void test_zero_depth_evaluates_position(void) {
    Position position;

    clear_position(&position);
    position_set_piece(&position, make_square(7, 0), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(0, 7), PIECE_BLACK_KING);
    position_set_piece(&position, make_square(6, 0), PIECE_WHITE_PAWN);
    position.side_to_move = COLOR_WHITE;

    assert(search_position(&position, 0, 0) > PAWN_VALUE);
}

static void test_search_captures_hanging_rook(void) {
    Position position;
    Move best_move;

    clear_position(&position);
    position_set_piece(&position, make_square(7, 0), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(7, 3), PIECE_WHITE_QUEEN);
    position_set_piece(&position, make_square(0, 3), PIECE_BLACK_ROOK);
    position_set_piece(&position, make_square(0, 7), PIECE_BLACK_KING);
    position.side_to_move = COLOR_WHITE;

    assert(search_position(&position, 1, &best_move) > 800);
    assert(best_move.from == make_square(7, 3));
    assert(best_move.to == make_square(0, 3));
}

static void test_search_detects_checkmate(void) {
    Position position;
    Move best_move;

    clear_position(&position);
    position_set_piece(&position, make_square(2, 2), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(1, 1), PIECE_WHITE_QUEEN);
    position_set_piece(&position, make_square(0, 0), PIECE_BLACK_KING);
    position.side_to_move = COLOR_BLACK;

    assert(search_position(&position, 1, &best_move) == -SEARCH_CHECKMATE);
    assert(best_move.from == NO_SQUARE);
    assert(best_move.to == NO_SQUARE);
}

static void test_iterative_search_reaches_requested_depth(void) {
    Position position;
    Move best_move;
    PrincipalVariation variation;
    int completed_depth;

    clear_position(&position);
    position_set_piece(&position, make_square(7, 0), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(7, 3), PIECE_WHITE_QUEEN);
    position_set_piece(&position, make_square(0, 3), PIECE_BLACK_ROOK);
    position_set_piece(&position, make_square(0, 7), PIECE_BLACK_KING);
    position.side_to_move = COLOR_WHITE;

    assert(search_iterative(
        &position,
        2,
        0,
        &best_move,
        &variation,
        &completed_depth
    ) > 0);
    assert(completed_depth == 2);
    assert(best_move.from == make_square(7, 3));
    assert(best_move.to == make_square(0, 3));
    assert(variation.count == 2);
    assert(variation.moves[0].from == best_move.from);
    assert(variation.moves[0].to == best_move.to);
}

static void test_quiescence_sees_a_recapture(void) {
    Position position;
    Move best_move;

    clear_position(&position);
    position_set_piece(&position, make_square(7, 0), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(7, 3), PIECE_WHITE_QUEEN);
    position_set_piece(&position, make_square(0, 3), PIECE_BLACK_ROOK);
    position_set_piece(&position, make_square(0, 4), PIECE_BLACK_QUEEN);
    position_set_piece(&position, make_square(0, 7), PIECE_BLACK_KING);
    position.side_to_move = COLOR_WHITE;

    assert(search_position(&position, 1, &best_move) < 0);
    assert(!(best_move.from == make_square(7, 3) &&
             best_move.to == make_square(0, 3)));
}

static void test_quiescence_keeps_starting_position_equal(void) {
    Position position;
    int score;

    set_starting_position(&position);
    score = search_position(&position, 5, 0);

    assert(score > -100);
    assert(score < 100);
}

static void test_search_returns_a_legal_move(void) {
    Position position;
    Move best_move;
    UndoState undo;

    clear_position(&position);
    position_set_piece(&position, make_square(7, 0), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(7, 3), PIECE_WHITE_QUEEN);
    position_set_piece(&position, make_square(0, 4), PIECE_BLACK_KING);
    position.side_to_move = COLOR_WHITE;

    search_position(&position, 1, &best_move);
    assert(make_move(&position, best_move, &undo));
    undo_move(&position, best_move, &undo);
}

static void test_search_restores_material(void) {
    Position position;
    PrincipalVariation variation;
    int completed_depth;

    set_starting_position(&position);
    search_iterative(&position, 5, 0, 0, &variation, &completed_depth);

    assert(completed_depth == 5);
    assert(evaluate_position(&position) == 0);
}

static void test_fifty_move_draw(void) {
    Position position;

    clear_position(&position);
    position_set_piece(&position, make_square(7, 0), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(0, 7), PIECE_BLACK_KING);
    position_set_piece(&position, make_square(4, 3), PIECE_WHITE_QUEEN);
    position.halfmove_clock = 100;

    assert(search_position(&position, 2, 0) == 0);
}

static void test_threefold_repetition_detection(void) {
    Position position;
    SearchContext context;

    set_starting_position(&position);
    initialize_search_context(&context, 0);
    assert(search_push_position(&context, &position));
    assert(search_push_position(&context, &position));
    assert(search_push_position(&context, &position));
    assert(search_is_draw(&context, &position));
    destroy_search_context(&context);
}

static void test_insufficient_material_draw(void) {
    Position position;

    clear_position(&position);
    position_set_piece(&position, make_square(7, 0), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(0, 7), PIECE_BLACK_KING);
    position_set_piece(&position, make_square(5, 2), PIECE_WHITE_BISHOP);

    assert(position_has_insufficient_material(&position));
    assert(search_position(&position, 3, 0) == 0);

    position_set_piece(&position, make_square(3, 0), PIECE_BLACK_BISHOP);
    assert(position_has_insufficient_material(&position));
}

static void test_search_reports_statistics(void) {
    Position position;
    Move best_move;
    PrincipalVariation variation;
    int completed_depth;

    set_starting_position(&position);
    callback_invocations = 0;
    search_iterative_with_callback(
        &position,
        5,
        0,
        &best_move,
        &variation,
        &completed_depth,
        capture_search_statistics,
        0
    );

    assert(callback_invocations == 5);
    assert(callback_statistics.nodes > 0);
    assert(callback_statistics.quiescence_nodes > 0);
    assert(callback_statistics.selective_depth >= completed_depth);
    assert(callback_statistics.elapsed_ms >= 0);
    assert(callback_statistics.beta_cutoffs > 0);
    assert(callback_statistics.first_move_beta_cutoffs <=
           callback_statistics.beta_cutoffs);
    assert(callback_statistics.late_move_reductions > 0);
    assert(callback_statistics.null_move_attempts > 0);
    assert(callback_statistics.null_move_cutoffs <=
           callback_statistics.null_move_attempts);
    assert(callback_statistics.reverse_futility_prunes +
           callback_statistics.late_move_prunes +
           callback_statistics.static_futility_prunes +
           callback_statistics.razoring_prunes > 0);
    assert(callback_statistics.delta_prunes + callback_statistics.see_prunes > 0);
    assert(callback_statistics.aspiration_researches <=
           callback_statistics.aspiration_failures);
}

static void test_node_limited_search_returns_a_legal_move(void) {
    Position position;
    Move best_move;
    UndoState undo;
    PrincipalVariation variation;
    int completed_depth;

    set_starting_position(&position);
    search_iterative_with_callback_and_node_limit(
        &position,
        20,
        0,
        1,
        &best_move,
        &variation,
        &completed_depth,
        0,
        0
    );

    assert(make_move(&position, best_move, &undo));
    undo_move(&position, best_move, &undo);
}

static void test_tactical_positions(void) {
    static const char *positions[] = {
        "3r3k/8/8/8/8/8/8/K2Q4 w - - 0 1",
        "6k1/5ppp/8/8/8/8/6PP/3Q2K1 w - - 0 1"
    };
    int index;

    for (index = 0; index < (int)(sizeof(positions) / sizeof(positions[0]));
         ++index) {
        Position position;
        Move best_move;

        assert(position_from_fen(&position, positions[index]));
        search_position(&position, 2, &best_move);
        assert(best_move.from == make_square(7, 3));
        assert(best_move.to == make_square(0, 3));
    }
}

static void test_mate_distance_score(void) {
    Position position;

    assert(position_from_fen(
        &position,
        "6k1/5ppp/8/8/8/8/6PP/3Q2K1 w - - 0 1"
    ));
    assert(search_position(&position, 2, 0) == SEARCH_CHECKMATE - 1);
}

int main(void) {
    test_zero_depth_evaluates_position();
    test_search_captures_hanging_rook();
    test_search_detects_checkmate();
    test_iterative_search_reaches_requested_depth();
    test_quiescence_sees_a_recapture();
    test_quiescence_keeps_starting_position_equal();
    test_search_returns_a_legal_move();
    test_search_restores_material();
    test_fifty_move_draw();
    test_threefold_repetition_detection();
    test_insufficient_material_draw();
    test_search_reports_statistics();
    test_node_limited_search_returns_a_legal_move();
    test_tactical_positions();
    test_mate_distance_score();
    return 0;
}
