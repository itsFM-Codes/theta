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
    // Cached because search probes the same position key several times.
    mutable uint64_t zobrist_key;
    mutable int zobrist_key_valid;
    mutable Color zobrist_side_to_move;
    mutable int zobrist_castling_rights;
    mutable int zobrist_en_passant_square;
} Position;

void clear_position(Position *position);
void set_starting_position(Position *position);

static inline Piece position_piece_at(
    const Position *position,
    int square
) {
    return position != 0 && is_valid_square(square)
        ? position->board[square]
        : PIECE_NONE;
}

static inline Piece position_piece_at_coordinates(
    const Position *position,
    int row,
    int column
) {
    return position_piece_at(position, make_square(row, column));
}

static inline int position_set_piece(
    Position *position,
    int square,
    Piece piece
) {
    if (position == 0 || !is_valid_square(square)) {
        return 0;
    }

    position->board[square] = piece;
    position->zobrist_key_valid = 0;
    return 1;
}

static inline int position_set_piece_at_coordinates(
    Position *position,
    int row,
    int column,
    Piece piece
) {
    return position_set_piece(position, make_square(row, column), piece);
}

#endif // POSITION_H
