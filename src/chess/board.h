#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#define BOARD_SIZE 8
#define SQUARE_COUNT (BOARD_SIZE * BOARD_SIZE)
#define NO_SQUARE (-1)

typedef enum Color {
    COLOR_WHITE,
    COLOR_BLACK,
    COLOR_NONE
} Color;

typedef enum PieceType {
    PIECE_TYPE_NONE,
    PIECE_TYPE_PAWN,
    PIECE_TYPE_KNIGHT,
    PIECE_TYPE_BISHOP,
    PIECE_TYPE_ROOK,
    PIECE_TYPE_QUEEN,
    PIECE_TYPE_KING
} PieceType;

typedef enum Piece {
    PIECE_NONE,
    PIECE_WHITE_PAWN,
    PIECE_WHITE_KNIGHT,
    PIECE_WHITE_BISHOP,
    PIECE_WHITE_ROOK,
    PIECE_WHITE_QUEEN,
    PIECE_WHITE_KING,
    PIECE_BLACK_PAWN,
    PIECE_BLACK_KNIGHT,
    PIECE_BLACK_BISHOP,
    PIECE_BLACK_ROOK,
    PIECE_BLACK_QUEEN,
    PIECE_BLACK_KING
} Piece;

static inline int is_valid_coordinate(int row, int column) {
    return row >= 0 && row < BOARD_SIZE &&
           column >= 0 && column < BOARD_SIZE;
}

static inline int is_valid_square(int square) {
    return square >= 0 && square < SQUARE_COUNT;
}

static inline int make_square(int row, int column) {
    return is_valid_coordinate(row, column)
        ? row * BOARD_SIZE + column
        : NO_SQUARE;
}

static inline int square_row(int square) {
    return is_valid_square(square) ? square / BOARD_SIZE : -1;
}

static inline int square_column(int square) {
    return is_valid_square(square) ? square % BOARD_SIZE : -1;
}

static inline Color piece_color(Piece piece) {
    if (piece >= PIECE_WHITE_PAWN && piece <= PIECE_WHITE_KING) {
        return COLOR_WHITE;
    }
    if (piece >= PIECE_BLACK_PAWN && piece <= PIECE_BLACK_KING) {
        return COLOR_BLACK;
    }
    return COLOR_NONE;
}

static inline Color opposite_color(Color color) {
    return color == COLOR_WHITE
        ? COLOR_BLACK
        : (color == COLOR_BLACK ? COLOR_WHITE : COLOR_NONE);
}

static inline PieceType piece_type(Piece piece) {
    if (piece == PIECE_NONE) {
        return PIECE_TYPE_NONE;
    }

    int normalized = piece > PIECE_WHITE_KING
        ? piece - PIECE_WHITE_KING
        : piece;
    return normalized >= PIECE_WHITE_PAWN &&
           normalized <= PIECE_WHITE_KING
        ? (PieceType)normalized
        : PIECE_TYPE_NONE;
}

#endif // BOARD_H
