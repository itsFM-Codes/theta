#include "pawn_structure.h"

#define DOUBLED_PAWN_PENALTY 12
#define ISOLATED_PAWN_PENALTY 10
#define SUPPORTED_PASSED_PAWN_BONUS 6
#define CONNECTED_PASSED_PAWN_BONUS 5
#define BLOCKED_PASSED_PAWN_PENALTY 8

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

static int pawn_is_supported(const Position *position, int square, Color color) {
    int row = square_row(square);
    int column = square_column(square);
    int supporter_row = color == COLOR_WHITE ? row + 1 : row - 1;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int file;

    if (supporter_row < 0 || supporter_row >= BOARD_SIZE) {
        return 0;
    }

    for (file = column - 1; file <= column + 1; file += 2) {
        if (file >= 0 && file < BOARD_SIZE &&
            position_piece_at_coordinates(position, supporter_row, file) == pawn) {
            return 1;
        }
    }

    return 0;
}

static int pawn_is_connected_passed(
    const Position *position,
    int square,
    Color color
) {
    int row = square_row(square);
    int column = square_column(square);
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int file;
    int connected_row;

    for (file = column - 1; file <= column + 1; file += 2) {
        if (file < 0 || file >= BOARD_SIZE) {
            continue;
        }

        for (connected_row = row - 1;
             connected_row <= row + 1;
             ++connected_row) {
            int connected_square;

            if (connected_row < 0 || connected_row >= BOARD_SIZE) {
                continue;
            }

            connected_square = make_square(connected_row, file);
            if (position_piece_at(position, connected_square) == pawn &&
                pawn_is_passed(position, connected_square, color)) {
                return 1;
            }
        }
    }

    return 0;
}

static int pawn_is_blocked(const Position *position, int square, Color color) {
    int row = square_row(square);
    int column = square_column(square);
    int front_row = color == COLOR_WHITE ? row - 1 : row + 1;

    return front_row >= 0 && front_row < BOARD_SIZE &&
           position_piece_at_coordinates(position, front_row, column) != PIECE_NONE;
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
            int advancement = color == COLOR_WHITE
                ? 6 - square_row(square)
                : square_row(square) - 1;

            score += passed_pawn_bonus(square, color);
            if (pawn_is_supported(position, square, color)) {
                score += SUPPORTED_PASSED_PAWN_BONUS + advancement * 2;
            }
            if (pawn_is_connected_passed(position, square, color)) {
                score += CONNECTED_PASSED_PAWN_BONUS + advancement * 2;
            }
            if (pawn_is_blocked(position, square, color)) {
                score -= BLOCKED_PASSED_PAWN_PENALTY + advancement * 2;
            }
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
