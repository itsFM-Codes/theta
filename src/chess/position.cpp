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

    position->side_to_move = COLOR_WHITE;
    position->castling_rights = CASTLING_NONE;
    position->en_passant_square = NO_SQUARE;
    position->halfmove_clock = 0;
    position->fullmove_number = 1;
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

Piece position_piece_at(const Position *position, int square) {
    if (position == 0 || !is_valid_square(square)) {
        return PIECE_NONE;
    }

    return position->board[square];
}

Piece position_piece_at_coordinates(
    const Position *position,
    int row,
    int column
) {
    return position_piece_at(position, make_square(row, column));
}

int position_set_piece(Position *position, int square, Piece piece) {
    if (position == 0 || !is_valid_square(square)) {
        return 0;
    }

    position->board[square] = piece;
    position->zobrist_key_valid = 0;
    return 1;
}

int position_set_piece_at_coordinates(
    Position *position,
    int row,
    int column,
    Piece piece
) {
    return position_set_piece(position, make_square(row, column), piece);
}
