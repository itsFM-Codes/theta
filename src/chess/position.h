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
    uint64_t occupied;
    uint64_t color_occupied[COLOR_NONE];
    uint64_t piece_occupied[PIECE_BLACK_KING + 1];
    Color side_to_move;
    int castling_rights;
    int en_passant_square;
    int halfmove_clock;
    int fullmove_number;
    int white_king_square;
    int black_king_square;
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
    Piece previous_piece;
    uint64_t square_mask;

    if (position == 0 || !is_valid_square(square)) {
        return 0;
    }

    previous_piece = position->board[square];
    square_mask = UINT64_C(1) << square;
    if (previous_piece != PIECE_NONE) {
        Color previous_color = piece_color(previous_piece);

        position->occupied &= ~square_mask;
        if (previous_color != COLOR_NONE) {
            position->color_occupied[previous_color] &= ~square_mask;
        }
        position->piece_occupied[previous_piece] &= ~square_mask;
    }

    position->board[square] = piece;
    if (piece != PIECE_NONE) {
        Color color = piece_color(piece);

        position->occupied |= square_mask;
        if (color != COLOR_NONE) {
            position->color_occupied[color] |= square_mask;
        }
        position->piece_occupied[piece] |= square_mask;
    }

    if (previous_piece == PIECE_WHITE_KING && piece != PIECE_WHITE_KING) {
        position->white_king_square = NO_SQUARE;
    }
    if (previous_piece == PIECE_BLACK_KING && piece != PIECE_BLACK_KING) {
        position->black_king_square = NO_SQUARE;
    }
    if (piece == PIECE_WHITE_KING) {
        position->white_king_square = square;
    }
    if (piece == PIECE_BLACK_KING) {
        position->black_king_square = square;
    }
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
