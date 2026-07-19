#include "evaluation.h"
#include "king_safety.h"
#include "mobility.h"
#include "pawn_structure.h"
#include "piece_activity.h"
#include "piece_square_tables.h"
#include "strategic.h"

#define MAX_PHASE 24

static int piece_value(Piece piece) {
    switch (piece_type(piece)) {
        case PIECE_TYPE_PAWN:
            return PAWN_VALUE;
        case PIECE_TYPE_KNIGHT:
            return KNIGHT_VALUE;
        case PIECE_TYPE_BISHOP:
            return BISHOP_VALUE;
        case PIECE_TYPE_ROOK:
            return ROOK_VALUE;
        case PIECE_TYPE_QUEEN:
            return QUEEN_VALUE;
        case PIECE_TYPE_KING:
            return KING_VALUE;
        case PIECE_TYPE_NONE:
            return 0;
    }

    return 0;
}

static int piece_phase(Piece piece) {
    switch (piece_type(piece)) {
        case PIECE_TYPE_KNIGHT:
        case PIECE_TYPE_BISHOP:
            return 1;
        case PIECE_TYPE_ROOK:
            return 2;
        case PIECE_TYPE_QUEEN:
            return 4;
        default:
            return 0;
    }
}

int evaluate_position_with_trace(
    const Position *position,
    EvaluationTrace *trace
) {
    int score = 0;
    int phase = 0;
    int endgame_weight;
    int square;

    if (position == 0) {
        return 0;
    }

    for (square = 0; square < SQUARE_COUNT; ++square) {
        phase += piece_phase(position_piece_at(position, square));
    }

    endgame_weight = (MAX_PHASE - phase) * 256 / MAX_PHASE;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        Piece piece = position_piece_at(position, square);
        int value = piece_value(piece) +
                    piece_square_value(piece, square, endgame_weight);

        if (piece_color(piece) == COLOR_WHITE) {
            score += value;
        } else if (piece_color(piece) == COLOR_BLACK) {
            score -= value;
        }
    }

    if (trace != 0) {
        trace->material_and_piece_square = score;
        trace->mobility = mobility_score(position);
        trace->pawn_structure = pawn_structure_score(position);
        trace->king_safety = king_safety_score(position, endgame_weight);
        trace->piece_activity = piece_activity_score(position, endgame_weight);
        trace->threats = threat_score(position);
        trace->space = space_score(position);
        score += trace->mobility + trace->pawn_structure +
            trace->king_safety + trace->piece_activity + trace->threats +
            trace->space;
    } else {
        score += mobility_score(position);
        score += pawn_structure_score(position);
        score += king_safety_score(position, endgame_weight);
        score += piece_activity_score(position, endgame_weight);
        score += threat_score(position);
        score += space_score(position);
    }

    if (position->side_to_move == COLOR_BLACK) {
        score = -score;
    }

    if (trace != 0) {
        trace->total = score;
    }
    return score;
}

int evaluate_position(const Position *position) {
    return evaluate_position_with_trace(position, 0);
}
