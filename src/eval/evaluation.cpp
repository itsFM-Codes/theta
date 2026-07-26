#include "evaluation.h"
#include "king_safety.h"
#include "mobility.h"
#include "pawn_structure.h"
#include "piece_activity.h"
#include "piece_square_tables.h"
#include "strategic.h"
#include "src/chess/zobrist.h"

#define EVALUATION_CACHE_SIZE (1 << 16)

typedef struct EvaluationCacheEntry {
    uint64_t key;
    unsigned int params_generation;
    int score;
    int valid;
} EvaluationCacheEntry;

static thread_local EvaluationCacheEntry
    evaluation_cache[EVALUATION_CACHE_SIZE];

static int piece_value(Piece piece) {
    return eval_piece_type_value(piece_type(piece));
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
    int material_score = 0;
    int piece_square_score = 0;
    int phase = 0;
    int endgame_weight;
    const EvalParams *params = current_eval_params();
    uint64_t pieces;

    if (position == 0) {
        return 0;
    }

    pieces = position->occupied;
    while (pieces != 0) {
        int square = pop_first_square(&pieces);

        phase += piece_phase(position_piece_at(position, square));
    }

    endgame_weight = (params->max_phase - phase) * 256 / params->max_phase;

    pieces = position->occupied;
    while (pieces != 0) {
        int square = pop_first_square(&pieces);
        Piece piece = position_piece_at(position, square);
        int material_value = piece_value(piece);
        int piece_square = piece_square_value(piece, square, endgame_weight);

        if (piece_color(piece) == COLOR_WHITE) {
            material_score += material_value;
            piece_square_score += piece_square;
        } else if (piece_color(piece) == COLOR_BLACK) {
            material_score -= material_value;
            piece_square_score -= piece_square;
        }
    }

    score = eval_scale_score(material_score, params->material_scale) +
            eval_scale_score(piece_square_score, params->piece_square_scale);

    if (trace != 0) {
        trace->material_and_piece_square = score;
        trace->mobility = eval_scale_score(
            mobility_score(position),
            params->mobility_scale
        );
        trace->pawn_structure = eval_scale_score(
            pawn_structure_score(position),
            params->pawn_structure_scale
        );
        trace->king_safety = eval_scale_score(
            king_safety_score(position, endgame_weight),
            params->king_safety_scale
        );
        trace->piece_activity = eval_scale_score(
            piece_activity_score(position, endgame_weight),
            params->piece_activity_scale
        );
        trace->threats = eval_scale_score(
            threat_score(position),
            params->threat_scale
        );
        trace->space = eval_scale_score(
            space_score(position),
            params->space_scale
        );
        score += trace->mobility + trace->pawn_structure +
            trace->king_safety + trace->piece_activity + trace->threats +
            trace->space;
    } else {
        score += eval_scale_score(mobility_score(position),
                                  params->mobility_scale);
        score += eval_scale_score(pawn_structure_score(position),
                                  params->pawn_structure_scale);
        score += eval_scale_score(king_safety_score(position, endgame_weight),
                                  params->king_safety_scale);
        score += eval_scale_score(piece_activity_score(
            position,
            endgame_weight
        ), params->piece_activity_scale);
        score += eval_scale_score(threat_score(position),
                                  params->threat_scale);
        score += eval_scale_score(space_score(position), params->space_scale);
    }

    if (trace != 0) {
        trace->tempo = position->side_to_move == COLOR_WHITE
            ? params->tempo_bonus
            : -params->tempo_bonus;
        score += trace->tempo;
    } else if (position->side_to_move == COLOR_WHITE) {
        score += params->tempo_bonus;
    } else if (position->side_to_move == COLOR_BLACK) {
        score -= params->tempo_bonus;
    }

    if (position->side_to_move == COLOR_BLACK) {
        score = -score;
        if (trace != 0) {
            trace->material_and_piece_square =
                -trace->material_and_piece_square;
            trace->mobility = -trace->mobility;
            trace->pawn_structure = -trace->pawn_structure;
            trace->king_safety = -trace->king_safety;
            trace->piece_activity = -trace->piece_activity;
            trace->threats = -trace->threats;
            trace->space = -trace->space;
            trace->tempo = -trace->tempo;
        }
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
    if (entry->valid && entry->key == key &&
        entry->params_generation == eval_params_generation()) {
        return entry->score;
    }

    score = evaluate_position_with_trace(position, 0);
    entry->key = key;
    entry->params_generation = eval_params_generation();
    entry->score = score;
    entry->valid = 1;
    return score;
}
