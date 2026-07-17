#include "mobility.h"

static int sliding_mobility(
    const Position *position,
    int row,
    int column,
    int row_step,
    int column_step,
    Color color
) {
    int count = 0;

    row += row_step;
    column += column_step;

    while (is_valid_coordinate(row, column)) {
        Piece piece = position_piece_at(position, make_square(row, column));

        if (piece_color(piece) == color) {
            break;
        }

        ++count;

        if (piece != PIECE_NONE) {
            break;
        }

        row += row_step;
        column += column_step;
    }

    return count;
}

static int piece_mobility(const Position *position, int square, PieceType type) {
    static const int BISHOP_DIRECTIONS[4][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}
    };
    static const int ROOK_DIRECTIONS[4][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    int count = 0;
    int direction;
    int row = square_row(square);
    int column = square_column(square);
    Piece piece = position_piece_at(position, square);
    Color color = piece_color(piece);

    if (type == PIECE_TYPE_BISHOP || type == PIECE_TYPE_QUEEN) {
        for (direction = 0; direction < 4; ++direction) {
            count += sliding_mobility(
                position,
                row,
                column,
                BISHOP_DIRECTIONS[direction][0],
                BISHOP_DIRECTIONS[direction][1],
                color
            );
        }
    }

    if (type == PIECE_TYPE_ROOK || type == PIECE_TYPE_QUEEN) {
        for (direction = 0; direction < 4; ++direction) {
            count += sliding_mobility(
                position,
                row,
                column,
                ROOK_DIRECTIONS[direction][0],
                ROOK_DIRECTIONS[direction][1],
                color
            );
        }
    }

    return count;
}

int mobility_score(const Position *position) {
    int score = 0;
    int square;

    if (position == 0) {
        return 0;
    }

    for (square = 0; square < SQUARE_COUNT; ++square) {
        Piece piece = position_piece_at(position, square);
        PieceType type = piece_type(piece);
        int weight = 0;
        int value;

        if (type == PIECE_TYPE_BISHOP) {
            weight = 3;
        } else if (type == PIECE_TYPE_ROOK) {
            weight = 2;
        } else if (type == PIECE_TYPE_QUEEN) {
            weight = 1;
        } else {
            continue;
        }

        value = piece_mobility(position, square, type) * weight;

        if (piece_color(piece) == COLOR_WHITE) {
            score += value;
        } else if (piece_color(piece) == COLOR_BLACK) {
            score -= value;
        }
    }

    return score;
}
