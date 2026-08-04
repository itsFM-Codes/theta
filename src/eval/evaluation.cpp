#include "evaluation.h"
#include "nnue.h"
#include "king_safety.h"
#include "mobility.h"
#include "pawn_structure.h"
#include "piece_activity.h"
#include "piece_square_tables.h"
#include "strategic.h"
#include "src/chess/movegen.h"
#include "src/chess/zobrist.h"

#define EVALUATION_CACHE_SIZE (1 << 17)

typedef struct EvaluationCacheEntry {
    uint64_t key;
    unsigned int params_generation;
    unsigned int nnue_generation;
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
    int white_king_square = NO_SQUARE;
    int black_king_square = NO_SQUARE;
    uint64_t white_attacks;
    uint64_t black_attacks;
    const EvalParams *params = current_eval_params();
    uint64_t pieces;
    int nnue_score;

    if (position == 0) {
        return 0;
    }

    if (nnue_evaluate(position, &nnue_score)) {
        if (trace != 0) {
            trace->material_and_piece_square = nnue_score;
            trace->mobility = 0;
            trace->pawn_structure = 0;
            trace->king_safety = 0;
            trace->piece_activity = 0;
            trace->threats = 0;
            trace->space = 0;
            trace->tempo = 0;
            trace->total = nnue_score;
        }
        return nnue_score;
    }

    pieces = position->occupied;
    while (pieces != 0) {
        int square = pop_first_square(&pieces);
        Piece piece = position_piece_at(position, square);
        int sign = piece_color(piece) == COLOR_WHITE ? 1 : -1;

        phase += piece_phase(piece);
        material_score += sign * piece_value(piece);
        if (piece_type(piece) == PIECE_TYPE_KING) {
            if (piece_color(piece) == COLOR_WHITE) {
                white_king_square = square;
            } else {
                black_king_square = square;
            }
        } else {
            piece_square_score += sign *
                piece_square_value(piece, square, 0);
        }
    }

    endgame_weight = (params->max_phase - phase) * 256 / params->max_phase;
    if (endgame_weight < 0) {
        endgame_weight = 0;
    } else if (endgame_weight > 256) {
        endgame_weight = 256;
    }

    if (is_valid_square(white_king_square)) {
        piece_square_score += piece_square_value(
            PIECE_WHITE_KING,
            white_king_square,
            endgame_weight
        );
    }
    if (is_valid_square(black_king_square)) {
        piece_square_score -= piece_square_value(
            PIECE_BLACK_KING,
            black_king_square,
            endgame_weight
        );
    }

    score = eval_scale_score(material_score, params->material_scale) +
            eval_scale_score(piece_square_score, params->piece_square_scale);
    white_attacks = position_attack_map(position, COLOR_WHITE);
    black_attacks = position_attack_map(position, COLOR_BLACK);

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
            king_safety_score_with_attacks(
                position,
                endgame_weight,
                white_attacks,
                black_attacks
            ),
            params->king_safety_scale
        );
        trace->piece_activity = eval_scale_score(
            piece_activity_score(position, endgame_weight),
            params->piece_activity_scale
        );
        trace->threats = eval_scale_score(
            threat_score_with_attacks(
                position,
                white_attacks,
                black_attacks
            ),
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
        score += eval_scale_score(
            king_safety_score_with_attacks(
                position,
                endgame_weight,
                white_attacks,
                black_attacks
            ),
            params->king_safety_scale
        );
        score += eval_scale_score(piece_activity_score(
            position,
            endgame_weight
        ), params->piece_activity_scale);
        score += eval_scale_score(
            threat_score_with_attacks(
                position,
                white_attacks,
                black_attacks
            ),
            params->threat_scale
        );
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

    {
        int scaled_score = score -
            score * position->halfmove_clock / 199;

        if (trace != 0 && position->halfmove_clock > 0) {
            int *terms[] = {
                &trace->material_and_piece_square,
                &trace->mobility,
                &trace->pawn_structure,
                &trace->king_safety,
                &trace->piece_activity,
                &trace->threats,
                &trace->space,
                &trace->tempo
            };
            int term_total = 0;
            int index;

            for (index = 0; index < 8; ++index) {
                *terms[index] -=
                    *terms[index] * position->halfmove_clock / 199;
                term_total += *terms[index];
            }
            trace->tempo += scaled_score - term_total;
        }
        score = scaled_score;
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
    unsigned int current_nnue_generation;

    if (position == 0) {
        return 0;
    }

    key = position_key(position);
    current_nnue_generation = nnue_generation();
    entry = &evaluation_cache[key & (EVALUATION_CACHE_SIZE - 1)];
    if (entry->valid && entry->key == key &&
        entry->params_generation == eval_params_generation() &&
        entry->nnue_generation == current_nnue_generation) {
        return entry->score;
    }

    score = evaluate_position_with_trace(position, 0);
    entry->key = key;
    entry->params_generation = eval_params_generation();
    entry->nnue_generation = current_nnue_generation;
    entry->score = score;
    entry->valid = 1;
    return score;
}
