#include "pawn_structure.h"

#define DOUBLED_PAWN_PENALTY 12
#define ISOLATED_PAWN_PENALTY 10

static int count_pawns_on_file(
    const Position *position,
    Color color,
    int column
) {
    int count = 0;
    int row;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;

    for (row = 0; row < BOARD_SIZE; ++row) {
        if (position_piece_at_coordinates(position, row, column) == pawn) {
            ++count;
        }
    }

    return count;
}

static int pawn_is_passed(const Position *position, int square, Color color) {
    int row = square_row(square);
    int column = square_column(square);
    int row_step = color == COLOR_WHITE ? -1 : 1;
    Piece opposing_pawn = color == COLOR_WHITE
        ? PIECE_BLACK_PAWN
        : PIECE_WHITE_PAWN;

    row += row_step;

    while (row >= 0 && row < BOARD_SIZE) {
        int file;

        for (file = column - 1; file <= column + 1; ++file) {
            if (file >= 0 && file < BOARD_SIZE &&
                position_piece_at_coordinates(position, row, file) == opposing_pawn) {
                return 0;
            }
        }

        row += row_step;
    }

    return 1;
}

static int passed_pawn_bonus(int square, Color color) {
    int row = square_row(square);
    int advancement = color == COLOR_WHITE ? 6 - row : row - 1;

    return 10 + advancement * 10;
}

static int side_pawn_structure_score(const Position *position, Color color) {
    int score = 0;
    int column;
    int square;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;

    for (column = 0; column < BOARD_SIZE; ++column) {
        int count = count_pawns_on_file(position, color, column);

        if (count > 1) {
            score -= (count - 1) * DOUBLED_PAWN_PENALTY;
        }
    }

    for (square = 0; square < SQUARE_COUNT; ++square) {
        int column;
        int has_adjacent_pawn = 0;

        if (position_piece_at(position, square) != pawn) {
            continue;
        }

        column = square_column(square);

        if (column > 0 && count_pawns_on_file(position, color, column - 1) > 0) {
            has_adjacent_pawn = 1;
        }

        if (column < BOARD_SIZE - 1 &&
            count_pawns_on_file(position, color, column + 1) > 0) {
            has_adjacent_pawn = 1;
        }

        if (!has_adjacent_pawn) {
            score -= ISOLATED_PAWN_PENALTY;
        }

        if (pawn_is_passed(position, square, color)) {
            score += passed_pawn_bonus(square, color);
        }
    }

    return score;
}

int pawn_structure_score(const Position *position) {
    if (position == 0) {
        return 0;
    }

    return side_pawn_structure_score(position, COLOR_WHITE) -
           side_pawn_structure_score(position, COLOR_BLACK);
}
