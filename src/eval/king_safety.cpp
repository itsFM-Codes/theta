#include "king_safety.h"

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

static int attack_material(
    const Position *position,
    Color color,
    const EvalParams *params
) {
    Piece knight = color == COLOR_WHITE
        ? PIECE_WHITE_KNIGHT
        : PIECE_BLACK_KNIGHT;
    Piece bishop = color == COLOR_WHITE
        ? PIECE_WHITE_BISHOP
        : PIECE_BLACK_BISHOP;
    Piece rook = color == COLOR_WHITE
        ? PIECE_WHITE_ROOK
        : PIECE_BLACK_ROOK;
    Piece queen = color == COLOR_WHITE
        ? PIECE_WHITE_QUEEN
        : PIECE_BLACK_QUEEN;

    return __builtin_popcountll(position->piece_occupied[knight]) *
               params->piece_values[PIECE_TYPE_KNIGHT] +
           __builtin_popcountll(position->piece_occupied[bishop]) *
               params->piece_values[PIECE_TYPE_BISHOP] +
           __builtin_popcountll(position->piece_occupied[rook]) *
               params->piece_values[PIECE_TYPE_ROOK] +
           __builtin_popcountll(position->piece_occupied[queen]) *
               params->piece_values[PIECE_TYPE_QUEEN];
}

static int find_king_square(const Position *position, Color color) {
    return color == COLOR_WHITE
        ? position->white_king_square
        : position->black_king_square;
}

static int absolute_value(int value) {
    return value < 0 ? -value : value;
}

static int square_distance(int first, int second) {
    int row_distance = absolute_value(square_row(first) - square_row(second));
    int column_distance = absolute_value(
        square_column(first) - square_column(second)
    );
    return row_distance > column_distance ? row_distance : column_distance;
}

static int king_attack_units(
    const Position *position,
    int king_square,
    Color defending_color,
    uint64_t attack_map,
    const EvalParams *params
) {
    Color attacking_color = opposite_color(defending_color);
    int king_row = square_row(king_square);
    int king_column = square_column(king_square);
    int units = 0;
    int row;
    int column;
    uint64_t attackers;

    for (row = king_row - 1; row <= king_row + 1; ++row) {
        for (column = king_column - 1; column <= king_column + 1; ++column) {
            int ring_square;
            if (!is_valid_coordinate(row, column) ||
                (row == king_row && column == king_column)) {
                continue;
            }
            ring_square = make_square(row, column);
            if ((attack_map & (UINT64_C(1) << ring_square)) != 0) {
                units += params->king_ring_attack_unit;
            }
        }
    }

    attackers = position->color_occupied[attacking_color];
    while (attackers != 0) {
        int square = __builtin_ctzll(attackers);
        Piece piece = position_piece_at(position, square);
        int distance;
        int proximity;

        attackers &= attackers - 1;
        distance = square_distance(square, king_square);
        proximity = 5 - distance;
        if (proximity <= 0) {
            continue;
        }

        switch (piece_type(piece)) {
            case PIECE_TYPE_KNIGHT: units += proximity * 2; break;
            case PIECE_TYPE_BISHOP: units += proximity; break;
            case PIECE_TYPE_ROOK: units += proximity; break;
            case PIECE_TYPE_QUEEN: units += proximity * 2; break;
            default: break;
        }
    }
    return units;
}

static int pawn_storm_danger(
    const Position *position,
    int king_square,
    Color defending_color
) {
    Color attacking_color = opposite_color(defending_color);
    Piece attacking_pawn = attacking_color == COLOR_WHITE
        ? PIECE_WHITE_PAWN
        : PIECE_BLACK_PAWN;
    int king_column = square_column(king_square);
    int danger = 0;
    uint64_t pawns;

    if (king_column > 2 && king_column < 5) {
        return 0;
    }

    pawns = position->piece_occupied[attacking_pawn];
    while (pawns != 0) {
        int square = __builtin_ctzll(pawns);
        int advancement;
        int proximity;

        pawns &= pawns - 1;
        if (absolute_value(square_column(square) - king_column) > 1) {
            continue;
        }
        advancement = attacking_color == COLOR_WHITE
            ? 6 - square_row(square)
            : square_row(square) - 1;
        proximity = 6 - square_distance(square, king_square);
        if (advancement > 0 && proximity > 0) {
            danger += advancement * 2 + proximity * 2;
        }
    }
    return danger;
}

static int major_piece_file_pressure(
    const Position *position,
    int king_square,
    Color defending_color,
    const EvalParams *params
) {
    Color attacking_color = opposite_color(defending_color);
    int king_column = square_column(king_square);
    int pressure = 0;
    uint64_t majors =
        position->piece_occupied[attacking_color == COLOR_WHITE
            ? PIECE_WHITE_ROOK
            : PIECE_BLACK_ROOK] |
        position->piece_occupied[attacking_color == COLOR_WHITE
            ? PIECE_WHITE_QUEEN
            : PIECE_BLACK_QUEEN];

    while (majors != 0) {
        int square = __builtin_ctzll(majors);
        Piece piece = position_piece_at(position, square);
        int file_distance;

        majors &= majors - 1;

        file_distance = absolute_value(square_column(square) - king_column);
        if (file_distance > 1 || file_has_pawn(
                position, square_column(square), defending_color
            )) {
            continue;
        }

        pressure += piece_type(piece) == PIECE_TYPE_QUEEN
            ? params->queen_file_pressure
            : params->rook_file_pressure;
    }

    return pressure;
}

static int side_king_safety_score(
    const Position *position,
    Color color,
    uint64_t enemy_attacks,
    const EvalParams *params
) {
    int score = 0;
    int king_square = find_king_square(position, color);
    int king_row;
    int king_column;
    int shield_row;
    int column;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int danger;
    int scale;

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
            score += params->pawn_shield_bonus;
        }

        own_pawn = file_has_pawn(position, column, color);
        opposing_pawn = file_has_pawn(position, column, opposite_color(color));

        if (!own_pawn) {
            score -= params->semi_open_file_penalty;

            if (!opposing_pawn) {
                score -= params->open_file_penalty;
            }
        }
    }

    danger = king_attack_units(
                 position,
                 king_square,
                 color,
                 enemy_attacks,
                 params
             ) +
             pawn_storm_danger(position, king_square, color) +
             major_piece_file_pressure(position, king_square, color, params);
    danger += danger * danger / params->king_danger_quadratic_divisor;
    if (danger > params->max_king_danger) {
        danger = params->max_king_danger;
    }
    score -= danger;

    scale = attack_material(
        position,
        opposite_color(color),
        params
    ) + 400;
    if (scale > 3200) {
        scale = 3200;
    }
    return score * scale / 3200;
}

int king_safety_score_with_attacks(
    const Position *position,
    int endgame_weight,
    uint64_t white_attacks,
    uint64_t black_attacks
) {
    int score;
    const EvalParams *params = current_eval_params();

    if (position == 0) {
        return 0;
    }

    (void)endgame_weight;

    score = side_king_safety_score(
        position,
        COLOR_WHITE,
        black_attacks,
        params
    ) - side_king_safety_score(
        position,
        COLOR_BLACK,
        white_attacks,
        params
    );

    return score;
}

int king_safety_score(const Position *position, int endgame_weight) {
    if (position == 0) {
        return 0;
    }

    return king_safety_score_with_attacks(
        position,
        endgame_weight,
        position_attack_map(position, COLOR_WHITE),
        position_attack_map(position, COLOR_BLACK)
    );
}
