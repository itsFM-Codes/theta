#ifndef POSITION_H
#define POSITION_H

#include <stdint.h>

#include "board.h"

#define CASTLING_NONE 0
#define CASTLING_WHITE_KING_SIDE (1 << 0)
#define CASTLING_WHITE_QUEEN_SIDE (1 << 1)
#define CASTLING_BLACK_KING_SIDE (1 << 2)
#define CASTLING_BLACK_QUEEN_SIDE (1 << 3)
#define CASTLING_ALL 15

typedef struct Position {
    Piece board[SQUARE_COUNT];
    Color side_to_move;
    int castling_rights;
    int en_passant_square;
    int halfmove_clock;
    int fullmove_number;
} Position;

void clear_position(Position *position);
void set_starting_position(Position *position);

Piece position_piece_at(const Position *position, int square);
Piece position_piece_at_coordinates(
    const Position *position,
    int row,
    int column
);

int position_set_piece(Position *position, int square, Piece piece);
int position_set_piece_at_coordinates(
    Position *position,
    int row,
    int column,
    Piece piece
);

#endif // POSITION_H
