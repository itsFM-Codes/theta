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

int is_valid_coordinate(int row, int column);
int is_valid_square(int square);

int make_square(int row, int column);
int square_row(int square);
int square_column(int square);

Color piece_color(Piece piece);
Color opposite_color(Color color);
PieceType piece_type(Piece piece);

#endif // BOARD_H
