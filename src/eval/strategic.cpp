#include "strategic.h"

#include <string.h>

#include "src/chess/movegen.h"
#include "evaluation.h"

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

static int side_threat_score(
    const Position *position,
    Color color,
    uint64_t friendly_attacks,
    uint64_t enemy_attacks,
    uint64_t friendly_pawn_attacks,
    const EvalParams *params
) {
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
            (friendly_pawn_attacks & (UINT64_C(1) << square)) != 0) {
            score += params->pawn_threat_base + value / 32;
        }

        if ((friendly_attacks & (UINT64_C(1) << square)) != 0 &&
            (enemy_attacks & (UINT64_C(1) << square)) == 0) {
            score += value / params->hanging_piece_divisor;
        }
    }
    return score;
}

int threat_score_with_attacks(
    const Position *position,
    uint64_t white_attacks,
    uint64_t black_attacks
) {
    const EvalParams *params = current_eval_params();

    if (position == 0) {
        return 0;
    }
    return side_threat_score(
        position,
        COLOR_WHITE,
        white_attacks,
        black_attacks,
        position_pawn_attack_map(position, COLOR_WHITE),
        params
    ) - side_threat_score(
        position,
        COLOR_BLACK,
        black_attacks,
        white_attacks,
        position_pawn_attack_map(position, COLOR_BLACK),
        params
    );
}

int threat_score(const Position *position) {
    if (position == 0) {
        return 0;
    }

    return threat_score_with_attacks(
        position,
        position_attack_map(position, COLOR_WHITE),
        position_attack_map(position, COLOR_BLACK)
    );
}

static int side_space_score(
    const Position *position,
    Color color,
    uint64_t friendly_pawn_attacks,
    uint64_t enemy_pawn_attacks,
    const EvalParams *params
) {
    int first_row = color == COLOR_WHITE ? 2 : 3;
    int last_row = color == COLOR_WHITE ? 4 : 5;
    int score = 0;
    int row;
    int column;

    for (row = first_row; row <= last_row; ++row) {
        for (column = 1; column < BOARD_SIZE - 1; ++column) {
            int square = make_square(row, column);
            Piece occupant = position_piece_at(position, square);
            uint64_t mask = UINT64_C(1) << square;

            if (piece_color(occupant) == color ||
                (friendly_pawn_attacks & mask) == 0 ||
                (enemy_pawn_attacks & mask) != 0) {
                continue;
            }
            score += params->safe_space_bonus;
        }
    }
    return score;
}

int space_score(const Position *position) {
    const EvalParams *params = current_eval_params();
    uint64_t white_pawn_attacks;
    uint64_t black_pawn_attacks;

    if (position == 0) {
        return 0;
    }
    white_pawn_attacks = position_pawn_attack_map(position, COLOR_WHITE);
    black_pawn_attacks = position_pawn_attack_map(position, COLOR_BLACK);
    return side_space_score(
        position,
        COLOR_WHITE,
        white_pawn_attacks,
        black_pawn_attacks,
        params
    ) - side_space_score(
        position,
        COLOR_BLACK,
        black_pawn_attacks,
        white_pawn_attacks,
        params
    );
}

static int advanced_piece_unit(PieceType type) {
    switch (type) {
        case PIECE_TYPE_PAWN: return 1;
        case PIECE_TYPE_KNIGHT:
        case PIECE_TYPE_BISHOP: return 3;
        case PIECE_TYPE_ROOK: return 5;
        case PIECE_TYPE_QUEEN: return 9;
        default: return 0;
    }
}

static uint64_t advanced_pawn_attacks_from_square(
    int square,
    Color color
) {
    int row;
    int column;
    int step;
    uint64_t attacks = 0;

    if (!is_valid_square(square) || color == COLOR_NONE) {
        return 0;
    }

    row = square_row(square);
    column = square_column(square);
    step = color == COLOR_WHITE ? -1 : 1;
    row += step;
    if (is_valid_coordinate(row, column - 1)) {
        attacks |= UINT64_C(1) << make_square(row, column - 1);
    }
    if (is_valid_coordinate(row, column + 1)) {
        attacks |= UINT64_C(1) << make_square(row, column + 1);
    }
    return attacks;
}

static uint64_t advanced_piece_attack_map(
    const Position *position,
    int square,
    Piece piece
) {
    PieceType type = piece_type(piece);

    if (type == PIECE_TYPE_PAWN) {
        return advanced_pawn_attacks_from_square(
            square,
            piece_color(piece)
        );
    }
    return position_piece_attack_map(position, square, type);
}

static void advanced_build_attack_state(
    const Position *position,
    uint64_t piece_attacks[SQUARE_COUNT],
    unsigned char attack_counts[COLOR_NONE][SQUARE_COUNT]
) {
    uint64_t pieces;

    memset(piece_attacks, 0, sizeof(uint64_t) * SQUARE_COUNT);
    memset(attack_counts, 0, sizeof(unsigned char) *
        COLOR_NONE * SQUARE_COUNT);
    pieces = position->occupied;
    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        Piece piece = position_piece_at(position, square);
        Color color = piece_color(piece);
        uint64_t attacks;

        pieces &= pieces - 1;
        attacks = advanced_piece_attack_map(position, square, piece);
        piece_attacks[square] = attacks;
        while (attacks != 0) {
            int target = __builtin_ctzll(attacks);

            attacks &= attacks - 1;
            if (color != COLOR_NONE && attack_counts[color][target] < 255) {
                attack_counts[color][target]++;
            }
        }
    }
}

static int advanced_passed_pawn_path_units(
    const Position *position,
    int square,
    Color color,
    uint64_t enemy_attacks
) {
    int row = square_row(square);
    int column = square_column(square);
    int step = color == COLOR_WHITE ? -1 : 1;
    Piece enemy_pawn = color == COLOR_WHITE
        ? PIECE_BLACK_PAWN
        : PIECE_WHITE_PAWN;
    int path_units = 0;

    for (row += step; row >= 0 && row < BOARD_SIZE; row += step) {
        int file;
        for (file = column - 1; file <= column + 1; ++file) {
            if (file >= 0 && file < BOARD_SIZE &&
                position_piece_at_coordinates(position, row, file) ==
                    enemy_pawn) {
                return 0;
            }
        }
    }

    for (row = square_row(square) + step;
         row >= 0 && row < BOARD_SIZE;
         row += step) {
        int next = make_square(row, column);
        int advancement = color == COLOR_WHITE ? 7 - row : row;

        if (position_piece_at(position, next) != PIECE_NONE) {
            break;
        }
        path_units += 1 + advancement / 2;
        if ((enemy_attacks & (UINT64_C(1) << next)) != 0) {
            path_units--;
        }
    }

    return path_units;
}

static int advanced_king_ring_units(
    const Position *position,
    Color defending_color,
    const unsigned char attack_counts[COLOR_NONE][SQUARE_COUNT]
) {
    int king_square = defending_color == COLOR_WHITE
        ? position->white_king_square
        : position->black_king_square;
    Color attacking_color = opposite_color(defending_color);
    int row;
    int column;
    int units = 0;

    if (!is_valid_square(king_square)) {
        return 0;
    }

    for (row = square_row(king_square) - 1;
         row <= square_row(king_square) + 1;
         ++row) {
        for (column = square_column(king_square) - 1;
             column <= square_column(king_square) + 1;
             ++column) {
            int square;

            if (!is_valid_coordinate(row, column) ||
                (row == square_row(king_square) &&
                 column == square_column(king_square))) {
                continue;
            }
            square = make_square(row, column);
            units += attack_counts[attacking_color][square];
        }
    }
    return units;
}

int advanced_classical_score(
    const Position *position,
    const struct ClassicalEvalState *state
) {
    const EvalParams *params = current_eval_params();
    uint64_t local_piece_attacks[SQUARE_COUNT];
    unsigned char local_attack_counts[COLOR_NONE][SQUARE_COUNT];
    const uint64_t *piece_attacks;
    const unsigned char (*attack_counts)[SQUARE_COUNT];
    uint64_t white_pawn_attacks;
    uint64_t black_pawn_attacks;
    uint64_t white_attacks;
    uint64_t black_attacks;
    uint64_t pieces;
    int safe_mobility = 0;
    int coordination = 0;
    int hanging = 0;
    int passed_path = 0;
    int king_ring;

    if (position == 0 ||
        (params->advanced_safe_mobility_bonus == 0 &&
         params->advanced_coordination_bonus == 0 &&
         params->advanced_king_ring_bonus == 0 &&
         params->advanced_hanging_bonus == 0 &&
         params->advanced_passed_path_bonus == 0)) {
        return 0;
    }

    if (state != 0 && state->valid &&
        state->params_generation == eval_params_generation()) {
        piece_attacks = state->piece_attacks;
        attack_counts = state->attack_counts;
    } else {
        advanced_build_attack_state(
            position,
            local_piece_attacks,
            local_attack_counts
        );
        piece_attacks = local_piece_attacks;
        attack_counts = local_attack_counts;
    }

    white_pawn_attacks = position_pawn_attack_map(position, COLOR_WHITE);
    black_pawn_attacks = position_pawn_attack_map(position, COLOR_BLACK);
    white_attacks = position_attack_map(position, COLOR_WHITE);
    black_attacks = position_attack_map(position, COLOR_BLACK);
    pieces = position->occupied;
    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        Piece piece = position_piece_at(position, square);
        Color color = piece_color(piece);
        Color enemy = opposite_color(color);
        PieceType type = piece_type(piece);
        int sign = color == COLOR_WHITE ? 1 : -1;
        int unit = advanced_piece_unit(type);
        uint64_t attacks = piece_attacks[square];
        uint64_t enemy_pawn_attacks = color == COLOR_WHITE
            ? black_pawn_attacks
            : white_pawn_attacks;
        int defenders;

        pieces &= pieces - 1;
        if (color == COLOR_NONE || type == PIECE_TYPE_KING ||
            type == PIECE_TYPE_NONE) {
            continue;
        }

        if (type != PIECE_TYPE_PAWN) {
            safe_mobility += sign * __builtin_popcountll(
                attacks & ~position->color_occupied[color] &
                ~enemy_pawn_attacks
            );
        }

        defenders = attack_counts[color][square];
        if (defenders > 0) {
            coordination += sign * unit * (defenders > 2 ? 2 : defenders);
        }

        if (type == PIECE_TYPE_PAWN) {
            passed_path += sign * advanced_passed_pawn_path_units(
                position,
                square,
                color,
                enemy == COLOR_WHITE ? white_attacks : black_attacks
            );
        }

        if (attack_counts[enemy][square] > defenders) {
            hanging -= sign * unit * (
                attack_counts[enemy][square] - defenders
            );
        }
    }

    king_ring = advanced_king_ring_units(
        position,
        COLOR_BLACK,
        attack_counts
    ) - advanced_king_ring_units(
        position,
        COLOR_WHITE,
        attack_counts
    );

    return safe_mobility * params->advanced_safe_mobility_bonus +
           coordination * params->advanced_coordination_bonus +
           king_ring * params->advanced_king_ring_bonus +
           hanging * params->advanced_hanging_bonus +
           passed_path * params->advanced_passed_path_bonus;
}
