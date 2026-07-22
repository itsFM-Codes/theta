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

static void test_pinned_recapturing_piece_is_ignored(void) {
    Position position;
    Move move = {
        make_square(4, 2),
        make_square(2, 3),
        PIECE_NONE,
        MOVE_FLAG_CAPTURE
    };

    clear_position(&position);
    position_set_piece(&position, make_square(7, 0), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(7, 4), PIECE_WHITE_ROOK);
    position_set_piece(&position, make_square(4, 2), PIECE_WHITE_KNIGHT);
    position_set_piece(&position, make_square(0, 4), PIECE_BLACK_KING);
    position_set_piece(&position, make_square(1, 4), PIECE_BLACK_BISHOP);
    position_set_piece(&position, make_square(2, 3), PIECE_BLACK_PAWN);
    position.side_to_move = COLOR_WHITE;

    assert(static_exchange_evaluation(&position, move) == PAWN_VALUE);
}

static void test_recapture_promotion_is_counted(void) {
    Position position;
    Move move = {
        make_square(6, 0),
        make_square(7, 0),
        PIECE_NONE,
        MOVE_FLAG_CAPTURE
    };

    clear_position(&position);
    position_set_piece(&position, make_square(7, 7), PIECE_WHITE_KING);
    position_set_piece(&position, make_square(6, 0), PIECE_WHITE_QUEEN);
    position_set_piece(&position, make_square(0, 7), PIECE_BLACK_KING);
    position_set_piece(&position, make_square(7, 0), PIECE_BLACK_KNIGHT);
    position_set_piece(&position, make_square(6, 1), PIECE_BLACK_PAWN);
    position.side_to_move = COLOR_WHITE;

    assert(static_exchange_evaluation(&position, move) < -QUEEN_VALUE);
}

int main(void) {
    test_winning_capture();
    test_losing_capture();
    test_pinned_recapturing_piece_is_ignored();
    test_recapture_promotion_is_counted();
    return 0;
}
