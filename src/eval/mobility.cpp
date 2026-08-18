#include "mobility.h"
#include "eval_params.h"
#include "src/chess/movegen.h"

static uint64_t piece_attacks_for(
    const Position *position,
    int square,
    PieceType type,
    const uint64_t *cached_piece_attacks
) {
    if (cached_piece_attacks != 0 && is_valid_square(square)) {
        return cached_piece_attacks[square];
    }
    return position_piece_attack_map(position, square, type);
}

static int piece_mobility(
    const Position *position,
    int square,
    PieceType type,
    const uint64_t *cached_piece_attacks
) {
    Piece piece = position_piece_at(position, square);
    Color color = piece_color(piece);
    uint64_t attacks = piece_attacks_for(
        position,
        square,
        type,
        cached_piece_attacks
    );

    return __builtin_popcountll(
        attacks & ~position->color_occupied[color]
    );
}

static int knight_mobility(
    const Position *position,
    int square,
    Color color,
    const uint64_t *cached_piece_attacks
) {
    return __builtin_popcountll(
        piece_attacks_for(
            position,
            square,
            PIECE_TYPE_KNIGHT,
            cached_piece_attacks
        ) &
        ~position->color_occupied[color]
    );
}

int mobility_score_with_piece_attacks(
    const Position *position,
    const uint64_t *cached_piece_attacks
) {
    int score = 0;
    uint64_t pieces;
    const EvalParams *params = current_eval_params();

    if (position == 0) {
        return 0;
    }

    pieces = position->occupied;
    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        Piece piece = position_piece_at(position, square);
        PieceType type = piece_type(piece);
        int weight = 0;
        int value;

        pieces &= pieces - 1;
        if (type == PIECE_TYPE_KNIGHT) {
            value = knight_mobility(
                        position,
                        square,
                        piece_color(piece),
                        cached_piece_attacks
                    ) *
                    params->knight_mobility_weight;
        } else if (type == PIECE_TYPE_BISHOP) {
            weight = params->bishop_mobility_weight;
        } else if (type == PIECE_TYPE_ROOK) {
            weight = params->rook_mobility_weight;
        } else if (type == PIECE_TYPE_QUEEN) {
            weight = params->queen_mobility_weight;
        } else {
            continue;
        }

        if (type != PIECE_TYPE_KNIGHT) {
            value = piece_mobility(
                        position,
                        square,
                        type,
                        cached_piece_attacks
                    ) * weight;
        }

        if (piece_color(piece) == COLOR_WHITE) {
            score += value;
        } else if (piece_color(piece) == COLOR_BLACK) {
            score -= value;
        }
    }

    return score;
}

int mobility_score(const Position *position) {
    return mobility_score_with_piece_attacks(position, 0);
}
