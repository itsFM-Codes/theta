#include <assert.h>

#include "src/chess/board.h"
#include "src/chess/position.h"
#include "src/engine/search.h"
#include "src/eval/evaluation.h"

static void test_zero_depth_evaluates_position(void) {
    Position position;

    clear_position(&position);
    position_set_piece(&position, make_square(7, 0), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(0, 7), PIECE_BLACK_KING);
    position_set_piece(&position, make_square(6, 0), PIECE_WHITE_PAWN);
    position.side_to_move = COLOR_WHITE;

    assert(search_position(&position, 0, 0) == PAWN_VALUE);
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

    assert(search_position(&position, 1, &best_move) == QUEEN_VALUE);
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

int main(void) {
    test_zero_depth_evaluates_position();
    test_search_captures_hanging_rook();
    test_search_detects_checkmate();
    return 0;
}
