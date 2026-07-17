#include <assert.h>

#include "src/chess/board.h"
#include "src/chess/position.h"
#include "src/engine/static_exchange.h"
#include "src/eval/evaluation.h"

static void set_kings(Position *position) {
    position_set_piece(position, make_square(7, 0), PIECE_WHITE_KING);
    position_set_piece(position, make_square(0, 7), PIECE_BLACK_KING);
}

static void test_winning_capture(void) {
    Position position;
    Move move = {make_square(7, 3), make_square(0, 3), PIECE_NONE, MOVE_FLAG_CAPTURE};

    clear_position(&position);
    set_kings(&position);
    position_set_piece(&position, make_square(7, 3), PIECE_WHITE_QUEEN);
    position_set_piece(&position, make_square(0, 3), PIECE_BLACK_ROOK);

    assert(static_exchange_evaluation(&position, move) == ROOK_VALUE);
}

static void test_losing_capture(void) {
    Position position;
    Move move = {make_square(7, 3), make_square(0, 3), PIECE_NONE, MOVE_FLAG_CAPTURE};

    clear_position(&position);
    set_kings(&position);
    position_set_piece(&position, make_square(7, 3), PIECE_WHITE_QUEEN);
    position_set_piece(&position, make_square(0, 3), PIECE_BLACK_ROOK);
    position_set_piece(&position, make_square(1, 4), PIECE_BLACK_BISHOP);

    assert(static_exchange_evaluation(&position, move) < 0);
}

int main(void) {
    test_winning_capture();
    test_losing_capture();
    return 0;
}
