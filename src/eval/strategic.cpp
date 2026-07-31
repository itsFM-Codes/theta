#include "strategic.h"

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
