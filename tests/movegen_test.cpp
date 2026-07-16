#include <assert.h>

#include "src/chess/movegen.h"

static int count_moves_with_flag(const MoveList *moves, int flag) {
    int count = 0;
    int index;

    for (index = 0; index < moves->count; ++index) {
        if (moves->moves[index].flags & flag) {
            count++;
        }
    }

    return count;
}

int main(void) {
    Position position;
    MoveList moves;

    set_starting_position(&position);
    generate_moves(&position, &moves);
    assert(moves.count == 20);
    assert(count_moves_with_flag(&moves, MOVE_FLAG_DOUBLE_PAWN) == 8);

    clear_position(&position);
    position_set_piece_at_coordinates(
        &position,
        4,
        3,
        PIECE_WHITE_KNIGHT
    );
    generate_moves(&position, &moves);
    assert(moves.count == 8);

    clear_position(&position);
    position_set_piece_at_coordinates(
        &position,
        1,
        3,
        PIECE_WHITE_PAWN
    );
    position_set_piece_at_coordinates(
        &position,
        0,
        2,
        PIECE_BLACK_ROOK
    );
    position_set_piece_at_coordinates(
        &position,
        0,
        4,
        PIECE_BLACK_ROOK
    );
    generate_moves(&position, &moves);
    assert(moves.count == 12);
    assert(count_moves_with_flag(&moves, MOVE_FLAG_PROMOTION) == 12);
    assert(count_moves_with_flag(&moves, MOVE_FLAG_CAPTURE) == 8);

    clear_position(&position);
    position.castling_rights =
        CASTLING_WHITE_KING_SIDE |
        CASTLING_WHITE_QUEEN_SIDE;
    position_set_piece_at_coordinates(
        &position,
        7,
        4,
        PIECE_WHITE_KING
    );
    position_set_piece_at_coordinates(
        &position,
        7,
        0,
        PIECE_WHITE_ROOK
    );
    position_set_piece_at_coordinates(
        &position,
        7,
        7,
        PIECE_WHITE_ROOK
    );
    generate_moves(&position, &moves);
    assert(count_moves_with_flag(&moves, MOVE_FLAG_CASTLE_KINGSIDE) == 1);
    assert(count_moves_with_flag(&moves, MOVE_FLAG_CASTLE_QUEENSIDE) == 1);

    return 0;
}
