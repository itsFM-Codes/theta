#include "strategic.h"

#include "src/chess/movegen.h"
#include "evaluation.h"

#define PAWN_THREAT_BASE 12
#define HANGING_PIECE_DIVISOR 25
#define SAFE_SPACE_BONUS 3

static int strategic_piece_value(Piece piece) {
    switch (piece_type(piece)) {
        case PIECE_TYPE_PAWN: return PAWN_VALUE;
        case PIECE_TYPE_KNIGHT: return KNIGHT_VALUE;
        case PIECE_TYPE_BISHOP: return BISHOP_VALUE;
        case PIECE_TYPE_ROOK: return ROOK_VALUE;
        case PIECE_TYPE_QUEEN: return QUEEN_VALUE;
        default: return 0;
    }
}

static int pawn_controls_square(
    const Position *position,
    Color color,
    int target_square
) {
    int row = square_row(target_square) + (color == COLOR_WHITE ? 1 : -1);
    int column = square_column(target_square);
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int file;

    if (row < 0 || row >= BOARD_SIZE) {
        return 0;
    }

    for (file = column - 1; file <= column + 1; file += 2) {
        if (file >= 0 && file < BOARD_SIZE &&
            position_piece_at_coordinates(position, row, file) == pawn) {
            return 1;
        }
    }
    return 0;
}

static int side_threat_score(const Position *position, Color color) {
    Color enemy = opposite_color(color);
    int score = 0;
    uint64_t targets = position->color_occupied[enemy];

    while (targets != 0) {
        int square = __builtin_ctzll(targets);
        Piece target = position_piece_at(position, square);
        PieceType type = piece_type(target);
        int value;

        targets &= targets - 1;
        if (type == PIECE_TYPE_KING || type == PIECE_TYPE_NONE) {
            continue;
        }

        value = strategic_piece_value(target);
        if (type != PIECE_TYPE_PAWN &&
            pawn_controls_square(position, color, square)) {
            score += PAWN_THREAT_BASE + value / 32;
        }

        if (is_square_attacked(position, square, color) &&
            !is_square_attacked(position, square, enemy)) {
            score += value / HANGING_PIECE_DIVISOR;
        }
    }
    return score;
}

int threat_score(const Position *position) {
    if (position == 0) {
        return 0;
    }
    return side_threat_score(position, COLOR_WHITE) -
           side_threat_score(position, COLOR_BLACK);
}

static int side_space_score(const Position *position, Color color) {
    Color enemy = opposite_color(color);
    int first_row = color == COLOR_WHITE ? 2 : 3;
    int last_row = color == COLOR_WHITE ? 4 : 5;
    int score = 0;
    int row;
    int column;

    for (row = first_row; row <= last_row; ++row) {
        for (column = 1; column < BOARD_SIZE - 1; ++column) {
            int square = make_square(row, column);
            Piece occupant = position_piece_at(position, square);

            if (piece_color(occupant) == color ||
                !pawn_controls_square(position, color, square) ||
                pawn_controls_square(position, enemy, square)) {
                continue;
            }
            score += SAFE_SPACE_BONUS;
        }
    }
    return score;
}

int space_score(const Position *position) {
    if (position == 0) {
        return 0;
    }
    return side_space_score(position, COLOR_WHITE) -
           side_space_score(position, COLOR_BLACK);
}
