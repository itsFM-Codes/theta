#include "piece_activity.h"

#define BISHOP_PAIR_BONUS 30
#define SEMI_OPEN_ROOK_BONUS 12
#define OPEN_ROOK_BONUS 8

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

static int side_piece_activity_score(const Position *position, Color color) {
    Piece bishop = color == COLOR_WHITE ? PIECE_WHITE_BISHOP : PIECE_BLACK_BISHOP;
    Piece rook = color == COLOR_WHITE ? PIECE_WHITE_ROOK : PIECE_BLACK_ROOK;
    int bishops = 0;
    int score = 0;
    int square;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        Piece piece = position_piece_at(position, square);

        if (piece == bishop) {
            bishops++;
        } else if (piece == rook) {
            int column = square_column(square);
            int own_pawn = file_has_pawn(position, column, color);
            int opposing_pawn = file_has_pawn(
                position,
                column,
                opposite_color(color)
            );

            if (!own_pawn) {
                score += SEMI_OPEN_ROOK_BONUS;

                if (!opposing_pawn) {
                    score += OPEN_ROOK_BONUS;
                }
            }
        }
    }

    if (bishops >= 2) {
        score += BISHOP_PAIR_BONUS;
    }

    return score;
}

int piece_activity_score(const Position *position) {
    if (position == 0) {
        return 0;
    }

    return side_piece_activity_score(position, COLOR_WHITE) -
           side_piece_activity_score(position, COLOR_BLACK);
}
