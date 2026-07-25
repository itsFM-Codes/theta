#include "king_safety.h"

#include "src/chess/movegen.h"

#define PAWN_SHIELD_BONUS 12
#define SEMI_OPEN_FILE_PENALTY 10
#define OPEN_FILE_PENALTY 5
#define KING_RING_ATTACK_UNIT 3
#define KING_DANGER_QUADRATIC_DIVISOR 12
#define MAX_KING_DANGER 180
#define ROOK_FILE_PRESSURE 7
#define QUEEN_FILE_PRESSURE 11
#define FILE_A_MASK UINT64_C(0x0101010101010101)

static int file_has_pawn(const Position *position, int column, Color color) {
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;

    if (column < 0 || column >= BOARD_SIZE) {
        return 0;
    }

    return (position->piece_occupied[pawn] & (FILE_A_MASK << column)) != 0;
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
    Color defending_color
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
            if (is_square_attacked(position, ring_square, attacking_color)) {
                units += KING_RING_ATTACK_UNIT;
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
    Color defending_color
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
            ? QUEEN_FILE_PRESSURE
            : ROOK_FILE_PRESSURE;
    }

    return pressure;
}

static int side_king_safety_score(const Position *position, Color color) {
    int score = 0;
    int king_square = find_king_square(position, color);
    int king_row;
    int king_column;
    int shield_row;
    int column;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int danger;

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

    danger = king_attack_units(position, king_square, color) +
             pawn_storm_danger(position, king_square, color) +
             major_piece_file_pressure(position, king_square, color);
    danger += danger * danger / KING_DANGER_QUADRATIC_DIVISOR;
    if (danger > MAX_KING_DANGER) {
        danger = MAX_KING_DANGER;
    }
    score -= danger;

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
