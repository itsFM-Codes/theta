#include "piece_activity.h"

#define BISHOP_PAIR_MIDDLEGAME_BONUS 22
#define BISHOP_PAIR_ENDGAME_BONUS 40
#define SEMI_OPEN_ROOK_BONUS 12
#define OPEN_ROOK_BONUS 8
#define ROOK_SEVENTH_RANK_BONUS 16
#define KNIGHT_OUTPOST_BONUS 18
#define BAD_BISHOP_PAWN_PENALTY 3
#define BAD_BISHOP_MOBILITY_PENALTY 8
#define TRAPPED_MINOR_PENALTY 12
#define FILE_A_MASK UINT64_C(0x0101010101010101)

static int file_has_pawn(const Position *position, int column, Color color) {
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;

    if (column < 0 || column >= BOARD_SIZE) {
        return 0;
    }

    return (position->piece_occupied[pawn] & (FILE_A_MASK << column)) != 0;
}

static int pawn_controls_square(
    const Position *position,
    Color color,
    int target_square
) {
    int target_row = square_row(target_square);
    int target_column = square_column(target_square);
    int pawn_row = color == COLOR_WHITE ? target_row + 1 : target_row - 1;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int file;

    if (pawn_row < 0 || pawn_row >= BOARD_SIZE) {
        return 0;
    }

    for (file = target_column - 1; file <= target_column + 1; file += 2) {
        if (file >= 0 && file < BOARD_SIZE &&
            position_piece_at_coordinates(position, pawn_row, file) == pawn) {
            return 1;
        }
    }

    return 0;
}

static int bishop_mobility_direction(
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
        Piece piece = position_piece_at_coordinates(position, row, column);

        if (piece_color(piece) == color) {
            break;
        }

        count++;
        if (piece != PIECE_NONE) {
            break;
        }

        row += row_step;
        column += column_step;
    }

    return count;
}

static int bishop_mobility(const Position *position, int square, Color color) {
    int row = square_row(square);
    int column = square_column(square);

    return bishop_mobility_direction(position, row, column, -1, -1, color) +
           bishop_mobility_direction(position, row, column, -1, 1, color) +
           bishop_mobility_direction(position, row, column, 1, -1, color) +
           bishop_mobility_direction(position, row, column, 1, 1, color);
}

static int knight_mobility(const Position *position, int square, Color color) {
    static const int OFFSETS[8][2] = {
        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
        {1, -2}, {1, 2}, {2, -1}, {2, 1}
    };
    int row = square_row(square);
    int column = square_column(square);
    int mobility = 0;
    int index;

    for (index = 0; index < 8; ++index) {
        int target_row = row + OFFSETS[index][0];
        int target_column = column + OFFSETS[index][1];
        if (is_valid_coordinate(target_row, target_column) &&
            piece_color(position_piece_at_coordinates(
                position,
                target_row,
                target_column
            )) != color) {
            mobility++;
        }
    }
    return mobility;
}

static int pawns_on_square_color(
    const Position *position,
    Color color,
    int square_color
) {
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int count = 0;
    uint64_t pawns = position->piece_occupied[pawn];

    while (pawns != 0) {
        int square = __builtin_ctzll(pawns);

        pawns &= pawns - 1;
        if (((square_row(square) + square_column(square)) & 1) ==
            square_color) {
            count++;
        }
    }

    return count;
}

static int bad_bishop_penalty(
    const Position *position,
    int square,
    Color color
) {
    int square_color = (square_row(square) + square_column(square)) & 1;
    int same_color_pawns = pawns_on_square_color(
        position,
        color,
        square_color
    );
    int penalty = same_color_pawns * BAD_BISHOP_PAWN_PENALTY;

    if (same_color_pawns >= 4 &&
        bishop_mobility(position, square, color) <= 5) {
        penalty += BAD_BISHOP_MOBILITY_PENALTY;
    }

    return penalty;
}

static int knight_is_outpost(
    const Position *position,
    int square,
    Color color
) {
    int row = square_row(square);

    if (color == COLOR_WHITE) {
        if (row < 2 || row > 4) {
            return 0;
        }
    } else if (row < 3 || row > 5) {
        return 0;
    }

    return pawn_controls_square(position, color, square) &&
           !pawn_controls_square(position, opposite_color(color), square);
}

static int rook_on_seventh_rank(int square, Color color) {
    int row = square_row(square);

    return (color == COLOR_WHITE && row == 1) ||
           (color == COLOR_BLACK && row == 6);
}

static int side_piece_activity_score(
    const Position *position,
    Color color,
    int endgame_weight
) {
    Piece bishop = color == COLOR_WHITE ? PIECE_WHITE_BISHOP : PIECE_BLACK_BISHOP;
    Piece knight = color == COLOR_WHITE ? PIECE_WHITE_KNIGHT : PIECE_BLACK_KNIGHT;
    Piece rook = color == COLOR_WHITE ? PIECE_WHITE_ROOK : PIECE_BLACK_ROOK;
    int bishops = 0;
    int score = 0;
    uint64_t pieces = position->piece_occupied[bishop] |
        position->piece_occupied[knight] |
        position->piece_occupied[rook];

    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        Piece piece = position_piece_at(position, square);

        pieces &= pieces - 1;
        if (piece == bishop) {
            int mobility = bishop_mobility(position, square, color);
            bishops++;
            score -= bad_bishop_penalty(position, square, color);
            if (mobility <= 2) {
                score -= (3 - mobility) * TRAPPED_MINOR_PENALTY;
            }
        } else if (piece == knight) {
            if (knight_is_outpost(position, square, color)) {
                score += KNIGHT_OUTPOST_BONUS;
            }
            if (knight_mobility(position, square, color) <= 1) {
                score -= TRAPPED_MINOR_PENALTY;
            }
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

            if (rook_on_seventh_rank(square, color)) {
                score += ROOK_SEVENTH_RANK_BONUS;
            }
        }
    }

    if (bishops >= 2) {
        score += BISHOP_PAIR_MIDDLEGAME_BONUS +
            (BISHOP_PAIR_ENDGAME_BONUS - BISHOP_PAIR_MIDDLEGAME_BONUS) *
            endgame_weight / 256;
    }

    return score;
}

int piece_activity_score(const Position *position, int endgame_weight) {
    if (position == 0) {
        return 0;
    }

    if (endgame_weight < 0) {
        endgame_weight = 0;
    } else if (endgame_weight > 256) {
        endgame_weight = 256;
    }

    return side_piece_activity_score(position, COLOR_WHITE, endgame_weight) -
           side_piece_activity_score(position, COLOR_BLACK, endgame_weight);
}
