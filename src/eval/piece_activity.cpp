#include "piece_activity.h"
#include "eval_params.h"
#include "src/chess/movegen.h"

#define FILE_A_MASK UINT64_C(0x0101010101010101)

static int file_has_pawn(const Position *position, int column, Color color) {
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;

    if (column < 0 || column >= BOARD_SIZE) {
        return 0;
    }

    return (position->piece_occupied[pawn] & (FILE_A_MASK << column)) != 0;
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
    Color color,
    const EvalParams *params
) {
    int square_color = (square_row(square) + square_column(square)) & 1;
    int same_color_pawns = pawns_on_square_color(
        position,
        color,
        square_color
    );
    int penalty = same_color_pawns * params->bad_bishop_pawn_penalty;

    if (same_color_pawns >= 4 &&
        bishop_mobility(position, square, color) <= 5) {
        penalty += params->bad_bishop_mobility_penalty;
    }

    return penalty;
}

static int knight_is_outpost(
    int square,
    Color color,
    uint64_t friendly_pawn_attacks,
    uint64_t enemy_pawn_attacks
) {
    int row = square_row(square);
    uint64_t mask = UINT64_C(1) << square;

    if (color == COLOR_WHITE) {
        if (row < 2 || row > 4) {
            return 0;
        }
    } else if (row < 3 || row > 5) {
        return 0;
    }

    return (friendly_pawn_attacks & mask) != 0 &&
           (enemy_pawn_attacks & mask) == 0;
}

static int rook_on_seventh_rank(int square, Color color) {
    int row = square_row(square);

    return (color == COLOR_WHITE && row == 1) ||
           (color == COLOR_BLACK && row == 6);
}

static int side_piece_activity_score(
    const Position *position,
    Color color,
    int endgame_weight,
    const EvalParams *params
) {
    Piece bishop = color == COLOR_WHITE ? PIECE_WHITE_BISHOP : PIECE_BLACK_BISHOP;
    Piece knight = color == COLOR_WHITE ? PIECE_WHITE_KNIGHT : PIECE_BLACK_KNIGHT;
    Piece rook = color == COLOR_WHITE ? PIECE_WHITE_ROOK : PIECE_BLACK_ROOK;
    int bishops = 0;
    int score = 0;
    uint64_t friendly_pawn_attacks = position_pawn_attack_map(
        position,
        color
    );
    uint64_t enemy_pawn_attacks = position_pawn_attack_map(
        position,
        opposite_color(color)
    );
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
            score -= bad_bishop_penalty(position, square, color, params);
            if (mobility <= 2) {
                score -= (3 - mobility) * params->trapped_minor_penalty;
            }
        } else if (piece == knight) {
            if (knight_is_outpost(
                    square,
                    color,
                    friendly_pawn_attacks,
                    enemy_pawn_attacks
                )) {
                score += params->knight_outpost_bonus;
            }
            if (knight_mobility(position, square, color) <= 1) {
                score -= params->trapped_minor_penalty;
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
                score += params->semi_open_rook_bonus;

                if (!opposing_pawn) {
                    score += params->open_rook_bonus;
                }
            }

            if (rook_on_seventh_rank(square, color)) {
                score += params->rook_seventh_rank_bonus;
            }
        }
    }

    if (bishops >= 2) {
        score += params->bishop_pair_middlegame_bonus +
            (params->bishop_pair_endgame_bonus -
             params->bishop_pair_middlegame_bonus) * endgame_weight / 256;
    }

    return score;
}

int piece_activity_score(const Position *position, int endgame_weight) {
    const EvalParams *params = current_eval_params();

    if (position == 0) {
        return 0;
    }

    if (endgame_weight < 0) {
        endgame_weight = 0;
    } else if (endgame_weight > 256) {
        endgame_weight = 256;
    }

    return side_piece_activity_score(
        position,
        COLOR_WHITE,
        endgame_weight,
        params
    ) - side_piece_activity_score(
        position,
        COLOR_BLACK,
        endgame_weight,
        params
    );
}
