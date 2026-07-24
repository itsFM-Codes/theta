#include "evaluation.h"
#include "king_safety.h"
#include "mobility.h"
#include "pawn_structure.h"
#include "piece_activity.h"
#include "piece_square_tables.h"
#include "strategic.h"
#include "src/chess/zobrist.h"

#define MAX_PHASE 24
#define EVALUATION_CACHE_SIZE (1 << 16)

typedef struct EvaluationCacheEntry {
    uint64_t key;
    int score;
    int valid;
} EvaluationCacheEntry;

static thread_local EvaluationCacheEntry
    evaluation_cache[EVALUATION_CACHE_SIZE];

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

static int pop_first_square(uint64_t *squares) {
    uint64_t value = *squares;
    int square;

    if (value == 0) {
        return NO_SQUARE;
    }

    square = __builtin_ctzll(value);
    *squares = value & (value - 1);
    return square;
}

int evaluate_position_with_trace(
    const Position *position,
    EvaluationTrace *trace
) {
    int score = 0;
    int phase = 0;
    int endgame_weight;
    uint64_t pieces;

    if (position == 0) {
        return 0;
    }

    pieces = position->occupied;
    while (pieces != 0) {
        int square = pop_first_square(&pieces);

        phase += piece_phase(position_piece_at(position, square));
    }

    endgame_weight = (MAX_PHASE - phase) * 256 / MAX_PHASE;

    pieces = position->occupied;
    while (pieces != 0) {
        int square = pop_first_square(&pieces);
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
    uint64_t key;
    EvaluationCacheEntry *entry;
    int score;

    if (position == 0) {
        return 0;
    }

    key = position_key(position);
    entry = &evaluation_cache[key & (EVALUATION_CACHE_SIZE - 1)];
    if (entry->valid && entry->key == key) {
        return entry->score;
    }

    score = evaluate_position_with_trace(position, 0);
    entry->key = key;
    entry->score = score;
    entry->valid = 1;
    return score;
}
