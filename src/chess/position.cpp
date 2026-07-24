#include "position.h"

static const Piece WHITE_BACK_RANK[BOARD_SIZE] = {
    PIECE_WHITE_ROOK,
    PIECE_WHITE_KNIGHT,
    PIECE_WHITE_BISHOP,
    PIECE_WHITE_QUEEN,
    PIECE_WHITE_KING,
    PIECE_WHITE_BISHOP,
    PIECE_WHITE_KNIGHT,
    PIECE_WHITE_ROOK
};

static const Piece BLACK_BACK_RANK[BOARD_SIZE] = {
    PIECE_BLACK_ROOK,
    PIECE_BLACK_KNIGHT,
    PIECE_BLACK_BISHOP,
    PIECE_BLACK_QUEEN,
    PIECE_BLACK_KING,
    PIECE_BLACK_BISHOP,
    PIECE_BLACK_KNIGHT,
    PIECE_BLACK_ROOK
};

void clear_position(Position *position) {
    int square;

    if (position == 0) {
        return;
    }

    for (square = 0; square < SQUARE_COUNT; ++square) {
        position->board[square] = PIECE_NONE;
    }

    position->occupied = 0;
    for (square = 0; square < COLOR_NONE; ++square) {
        position->color_occupied[square] = 0;
    }
    for (square = 0; square <= PIECE_BLACK_KING; ++square) {
        position->piece_occupied[square] = 0;
    }

    position->side_to_move = COLOR_WHITE;
    position->castling_rights = CASTLING_NONE;
    position->en_passant_square = NO_SQUARE;
    position->halfmove_clock = 0;
    position->fullmove_number = 1;
    position->white_king_square = NO_SQUARE;
    position->black_king_square = NO_SQUARE;
    position->zobrist_key = 0;
    position->zobrist_key_valid = 0;
    position->zobrist_side_to_move = COLOR_NONE;
    position->zobrist_castling_rights = CASTLING_NONE;
    position->zobrist_en_passant_square = NO_SQUARE;
}

void set_starting_position(Position *position) {
    int column;

    if (position == 0) {
        return;
    }

    clear_position(position);

    for (column = 0; column < BOARD_SIZE; ++column) {
        position_set_piece_at_coordinates(
            position,
            0,
            column,
            BLACK_BACK_RANK[column]
        );
        position_set_piece_at_coordinates(
            position,
            1,
            column,
            PIECE_BLACK_PAWN
        );
        position_set_piece_at_coordinates(
            position,
            6,
            column,
            PIECE_WHITE_PAWN
        );
        position_set_piece_at_coordinates(
            position,
            7,
            column,
            WHITE_BACK_RANK[column]
        );
    }

    position->castling_rights = CASTLING_ALL;
}
