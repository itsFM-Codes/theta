#include <assert.h>

#include "src/chess/position.h"

int main(void) {
    Position position;
    int square;

    set_starting_position(&position);

    assert(position_piece_at_coordinates(&position, 0, 0) == PIECE_BLACK_ROOK);
    assert(position_piece_at_coordinates(&position, 0, 4) == PIECE_BLACK_KING);
    assert(position_piece_at_coordinates(&position, 1, 3) == PIECE_BLACK_PAWN);
    assert(position_piece_at_coordinates(&position, 4, 4) == PIECE_NONE);
    assert(position_piece_at_coordinates(&position, 6, 3) == PIECE_WHITE_PAWN);
    assert(position_piece_at_coordinates(&position, 7, 4) == PIECE_WHITE_KING);
    assert(position.side_to_move == COLOR_WHITE);
    assert(position.castling_rights == CASTLING_ALL);

    assert(piece_color(PIECE_WHITE_QUEEN) == COLOR_WHITE);
    assert(piece_color(PIECE_BLACK_KNIGHT) == COLOR_BLACK);
    assert(piece_color(PIECE_NONE) == COLOR_NONE);
    assert(piece_type(PIECE_BLACK_BISHOP) == PIECE_TYPE_BISHOP);

    assert(!position_set_piece(&position, -1, PIECE_WHITE_PAWN));
    assert(position_piece_at(&position, -1) == PIECE_NONE);

    clear_position(&position);
    for (square = 0; square < SQUARE_COUNT; ++square) {
        assert(position.board[square] == PIECE_NONE);
    }

    assert(position.castling_rights == CASTLING_NONE);
    assert(position.en_passant_square == NO_SQUARE);
    return 0;
}
