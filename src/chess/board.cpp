#include "board.h"

int is_valid_coordinate(int row, int column) {
    return row >= 0 && row < BOARD_SIZE &&
           column >= 0 && column < BOARD_SIZE;
}

int is_valid_square(int square) {
    return square >= 0 && square < SQUARE_COUNT;
}

int make_square(int row, int column) {
    if (!is_valid_coordinate(row, column)) {
        return NO_SQUARE;
    }

    return row * BOARD_SIZE + column;
}

int square_row(int square) {
    return is_valid_square(square) ? square / BOARD_SIZE : -1;
}

int square_column(int square) {
    return is_valid_square(square) ? square % BOARD_SIZE : -1;
}

Color piece_color(Piece piece) {
    if (piece >= PIECE_WHITE_PAWN && piece <= PIECE_WHITE_KING) {
        return COLOR_WHITE;
    }

    if (piece >= PIECE_BLACK_PAWN && piece <= PIECE_BLACK_KING) {
        return COLOR_BLACK;
    }

    return COLOR_NONE;
}

Color opposite_color(Color color) {
    if (color == COLOR_WHITE) {
        return COLOR_BLACK;
    }

    if (color == COLOR_BLACK) {
        return COLOR_WHITE;
    }

    return COLOR_NONE;
}

PieceType piece_type(Piece piece) {
    switch (piece) {
        case PIECE_WHITE_PAWN:
        case PIECE_BLACK_PAWN:
            return PIECE_TYPE_PAWN;
        case PIECE_WHITE_KNIGHT:
        case PIECE_BLACK_KNIGHT:
            return PIECE_TYPE_KNIGHT;
        case PIECE_WHITE_BISHOP:
        case PIECE_BLACK_BISHOP:
            return PIECE_TYPE_BISHOP;
        case PIECE_WHITE_ROOK:
        case PIECE_BLACK_ROOK:
            return PIECE_TYPE_ROOK;
        case PIECE_WHITE_QUEEN:
        case PIECE_BLACK_QUEEN:
            return PIECE_TYPE_QUEEN;
        case PIECE_WHITE_KING:
        case PIECE_BLACK_KING:
            return PIECE_TYPE_KING;
        case PIECE_NONE:
            return PIECE_TYPE_NONE;
    }

    return PIECE_TYPE_NONE;
}
