#include <assert.h>

#include "src/chess/fen.h"
#include "src/chess/movegen.h"

int main(void) {
    Position position;
    MoveList moves;
    const char *starting_fen =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR "
        "w KQkq - 0 1";

    assert(position_from_fen(&position, starting_fen));
    assert(position_piece_at_coordinates(&position, 0, 0) == PIECE_BLACK_ROOK);
    assert(position_piece_at_coordinates(&position, 7, 4) == PIECE_WHITE_KING);
    assert(position.side_to_move == COLOR_WHITE);
    assert(position.castling_rights == CASTLING_ALL);
    assert(position.en_passant_square == NO_SQUARE);

    generate_legal_moves(&position, &moves);
    assert(moves.count == 20);

    assert(!position_from_fen(&position, "not a fen"));
    return 0;
}
