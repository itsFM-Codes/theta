#include "king_safety.h"

#define PAWN_SHIELD_BONUS 12
#define SEMI_OPEN_FILE_PENALTY 10
#define OPEN_FILE_PENALTY 5

static int file_has_pawn(const Position *position, int column, Color color) {
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int row;

    for (row = 0; row < BOARD_SIZE; ++row) {
        if (position_piece_at_coordinates(position, row, column) == pawn) {
            return 1;
        }
    }

    return 0;
}

static int find_king_square(const Position *position, Color color) {
    Piece king = color == COLOR_WHITE ? PIECE_WHITE_KING : PIECE_BLACK_KING;
    int square;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        if (position_piece_at(position, square) == king) {
            return square;
        }
    }

    return NO_SQUARE;
}

static int side_king_safety_score(const Position *position, Color color) {
    int score = 0;
    int king_square = find_king_square(position, color);
    int king_row;
    int king_column;
    int shield_row;
    int column;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;

    if (!is_valid_square(king_square)) {
        return 0;
    }

    king_row = square_row(king_square);
    king_column = square_column(king_square);
    shield_row = king_row + (color == COLOR_WHITE ? -1 : 1);

    for (column = king_column - 1; column <= king_column + 1; ++column) {
        int own_pawn;
        int opposing_pawn;

        if (column < 0 || column >= BOARD_SIZE) {
            continue;
        }

        if (is_valid_coordinate(shield_row, column) &&
            position_piece_at_coordinates(position, shield_row, column) == pawn) {
            score += PAWN_SHIELD_BONUS;
        }

        own_pawn = file_has_pawn(position, column, color);
        opposing_pawn = file_has_pawn(position, column, opposite_color(color));

        if (!own_pawn) {
            score -= SEMI_OPEN_FILE_PENALTY;

            if (!opposing_pawn) {
                score -= OPEN_FILE_PENALTY;
            }
        }
    }

    return score;
}

int king_safety_score(const Position *position, int endgame_weight) {
    int score;

    if (position == 0) {
        return 0;
    }

    if (endgame_weight < 0) {
        endgame_weight = 0;
    } else if (endgame_weight > 256) {
        endgame_weight = 256;
    }

    score = side_king_safety_score(position, COLOR_WHITE) -
            side_king_safety_score(position, COLOR_BLACK);

    return score * (256 - endgame_weight) / 256;
}
