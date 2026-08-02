#include "mobility.h"
#include "eval_params.h"
#include "src/chess/movegen.h"

static int piece_mobility(const Position *position, int square, PieceType type) {
    Piece piece = position_piece_at(position, square);
    Color color = piece_color(piece);
    uint64_t attacks;

    attacks = position_piece_attack_map(position, square, type);
    return __builtin_popcountll(
        attacks & ~position->color_occupied[color]
    );
}

static int knight_mobility(const Position *position, int square, Color color) {
    return __builtin_popcountll(
        position_piece_attack_map(position, square, PIECE_TYPE_KNIGHT) &
        ~position->color_occupied[color]
    );
}

int mobility_score(const Position *position) {
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
            value = knight_mobility(position, square, piece_color(piece)) *
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
            value = piece_mobility(position, square, type) * weight;
        }

        if (piece_color(piece) == COLOR_WHITE) {
            score += value;
        } else if (piece_color(piece) == COLOR_BLACK) {
            score -= value;
        }
    }

    return score;
}
