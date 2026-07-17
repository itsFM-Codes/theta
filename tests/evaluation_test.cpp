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

    assert(evaluate_position(&position) == 280);
}

static void test_piece_square_tables(void) {
    Position corner_position;
    Position center_position;

    clear_position(&corner_position);
    position_set_piece(&corner_position, 56, PIECE_WHITE_KNIGHT);
    corner_position.side_to_move = COLOR_WHITE;

    clear_position(&center_position);
    position_set_piece(&center_position, 35, PIECE_WHITE_KNIGHT);
    center_position.side_to_move = COLOR_WHITE;

    assert(evaluate_position(&center_position) >
           evaluate_position(&corner_position));
}

int main(void) {
    test_starting_position();
    test_side_to_move_score();
    test_material_difference();
    test_piece_square_tables();
    return 0;
}
