#include <assert.h>

#include "src/eval/evaluation.h"

static void test_starting_position(void) {
    Position position;

    set_starting_position(&position);
    assert(evaluate_position(&position) == 0);
}

static void test_side_to_move_score(void) {
    Position position;

    clear_position(&position);
    position_set_piece(&position, 0, PIECE_WHITE_PAWN);

    position.side_to_move = COLOR_WHITE;
    assert(evaluate_position(&position) == PAWN_VALUE);

    position.side_to_move = COLOR_BLACK;
    assert(evaluate_position(&position) == -PAWN_VALUE);
}

static void test_material_difference(void) {
    Position position;

    clear_position(&position);
    position_set_piece(&position, 0, PIECE_WHITE_QUEEN);
    position_set_piece(&position, 1, PIECE_BLACK_ROOK);
    position_set_piece(&position, 2, PIECE_BLACK_PAWN);

    assert(evaluate_position(&position) == 300);
}

int main(void) {
    test_starting_position();
    test_side_to_move_score();
    test_material_difference();
    return 0;
}
