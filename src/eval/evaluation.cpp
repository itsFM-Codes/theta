#include "evaluation.h"

#include <string.h>

#include "nnue.h"
#include "king_safety.h"
#include "mobility.h"
#include "pawn_structure.h"
#include "piece_activity.h"
#include "piece_square_tables.h"
#include "strategic.h"
#include "stockfish_classical.h"
#include "eval_features.h"
#include "src/chess/movegen.h"
#include "src/chess/move.h"
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

static int legacy_scale_for_term(const EvalParams *params, int term) {
    if (params == 0) {
        return 256;
    }

    switch (term) {
        case EVAL_FEATURE_MATERIAL:
            return params->material_scale;
        case EVAL_FEATURE_PIECE_SQUARE:
            return params->piece_square_scale;
        case EVAL_FEATURE_MOBILITY:
            return params->mobility_scale;
        case EVAL_FEATURE_PAWN_STRUCTURE:
            return params->pawn_structure_scale;
        case EVAL_FEATURE_KING_SAFETY:
            return params->king_safety_scale;
        case EVAL_FEATURE_PIECE_ACTIVITY:
            return params->piece_activity_scale;
        case EVAL_FEATURE_THREATS:
            return params->threat_scale;
        case EVAL_FEATURE_SPACE:
            return params->space_scale;
        case EVAL_FEATURE_TEMPO:
            return params->tempo_bonus;
        default:
            return 256;
    }
}

static int phase_scale_is_default(const EvalParams *params, int term) {
    int default_scale = term == EVAL_FEATURE_TEMPO ? 0 : 256;

    return params != 0 && term >= 0 && term < EVAL_FEATURE_COUNT &&
        params->phase_scales[term][0] == default_scale &&
        params->phase_scales[term][1] == default_scale;
}

static int phase_scale_for_term(int term, int endgame_weight) {
    const EvalParams *params = current_eval_params();
    int middlegame_scale;
    int endgame_scale;

    if (term < 0 || term >= EVAL_FEATURE_COUNT) {
        return 256;
    }
    if (endgame_weight < 0) {
        endgame_weight = 0;
    } else if (endgame_weight > 256) {
        endgame_weight = 256;
    }

    /* Preserve the tuned pre-phase configuration unless a phase-specific
     * value was actually supplied. This keeps the new phase path backward
     * compatible with existing configs and makes A/B tests meaningful. */
    if (phase_scale_is_default(params, term)) {
        return legacy_scale_for_term(params, term);
    }

    middlegame_scale = params->phase_scales[term][0];
    endgame_scale = params->phase_scales[term][1];
    return (middlegame_scale * (256 - endgame_weight) +
            endgame_scale * endgame_weight) / 256;
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

static int classical_fast_path_enabled(void) {
    static unsigned int cached_generation = 0;
    static int cached_value = 0;
    const EvalParams *params = current_eval_params();
    unsigned int generation = eval_params_generation();
    int term;

    if (params == 0) {
        return 0;
    }
    if (cached_generation == generation) {
        return cached_value;
    }
    for (term = 0; term < EVAL_FEATURE_COUNT; ++term) {
        int default_scale = term == EVAL_FEATURE_TEMPO ? 0 : 256;

        if (params->phase_scales[term][0] != default_scale ||
            params->phase_scales[term][1] != default_scale) {
            cached_generation = generation;
            cached_value = 0;
            return cached_value;
        }
    }
    cached_value = params->advanced_safe_mobility_bonus == 0 &&
        params->advanced_coordination_bonus == 0 &&
        params->advanced_king_ring_bonus == 0 &&
        params->advanced_hanging_bonus == 0 &&
        params->advanced_passed_path_bonus == 0 &&
        params->stockfish_classical_scale == 0 &&
        params->stockfish_piece_scale == 0 &&
        params->stockfish_mobility_scale == 0 &&
        params->stockfish_king_scale == 0 &&
        params->stockfish_threat_scale == 0 &&
        params->stockfish_passed_scale == 0 &&
        params->stockfish_space_scale == 0 &&
        params->stockfish_classical_replace == 0;
    cached_generation = generation;
    return cached_value;
}

/* Keep the default HCE path as small as the original evaluator. Optional
 * phase/experimental terms use the more general path below, but the normal
 * production configuration should not pay their dispatch cost per node. */
static int evaluate_legacy_position_with_trace(
    const Position *position,
    const ClassicalEvalState *state,
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
    int mobility_value;
    int pawn_structure_value;
    int king_safety_value;
    int piece_activity_value;
    int threats_value;
    int space_value;
    int tempo_value;
    const EvalParams *params = current_eval_params();
    int use_state = state != 0 && state->valid &&
        state->params_generation == eval_params_generation();
    uint64_t pieces;

    if (position == 0) {
        return 0;
    }

    if (use_state) {
        material_score = state->material_score;
        piece_square_score = state->piece_square_score_mg;
        phase = state->phase;
        white_king_square = position->white_king_square;
        black_king_square = position->black_king_square;
        white_attacks = state->attack_maps[COLOR_WHITE];
        black_attacks = state->attack_maps[COLOR_BLACK];
    } else {
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

        white_attacks = position_attack_map(position, COLOR_WHITE);
        black_attacks = position_attack_map(position, COLOR_BLACK);
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
    mobility_value = eval_scale_score(
        mobility_score_with_piece_attacks(
            position,
            use_state ? state->piece_attacks : 0
        ),
        params->mobility_scale
    );
    pawn_structure_value = eval_scale_score(
        pawn_structure_score(position),
        params->pawn_structure_scale
    );
    king_safety_value = eval_scale_score(
        king_safety_score_with_attacks(
            position,
            endgame_weight,
            white_attacks,
            black_attacks
        ),
        params->king_safety_scale
    );
    piece_activity_value = eval_scale_score(
        piece_activity_score_with_piece_attacks(
            position,
            endgame_weight,
            use_state ? state->piece_attacks : 0
        ),
        params->piece_activity_scale
    );
    threats_value = eval_scale_score(
        threat_score_with_attacks(
            position,
            white_attacks,
            black_attacks
        ),
        params->threat_scale
    );
    space_value = eval_scale_score(
        space_score(position),
        params->space_scale
    );

    if (trace != 0) {
        trace->material_and_piece_square = score;
        trace->mobility = mobility_value;
        trace->pawn_structure = pawn_structure_value;
        trace->king_safety = king_safety_value;
        trace->piece_activity = piece_activity_value;
        trace->threats = threats_value;
        trace->space = space_value;
        trace->advanced = 0;
        trace->stockfish_classical = 0;
        trace->stockfish_piece = 0;
        trace->stockfish_mobility = 0;
        trace->stockfish_king = 0;
        trace->stockfish_threats = 0;
        trace->stockfish_passed = 0;
        trace->stockfish_space = 0;
        score += trace->mobility + trace->pawn_structure +
            trace->king_safety + trace->piece_activity + trace->threats +
            trace->space;
    } else {
        score += mobility_value + pawn_structure_value +
            king_safety_value + piece_activity_value + threats_value +
            space_value;
    }

    tempo_value = position->side_to_move == COLOR_WHITE
        ? params->tempo_bonus
        : position->side_to_move == COLOR_BLACK
            ? -params->tempo_bonus
            : 0;
    if (trace != 0) {
        trace->tempo = tempo_value;
    }
    score += tempo_value;

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
                &trace->tempo,
                &trace->advanced
            };
            int term_total = 0;
            int index;

            for (index = 0; index < 9; ++index) {
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

static int evaluate_classical_position_with_state(
    const Position *position,
    const ClassicalEvalState *state,
    EvaluationTrace *trace
) {
    int score = 0;
    int material_score = 0;
    int piece_square_score_mg = 0;
    int piece_square_score_eg = 0;
    int piece_square_score;
    int phase = 0;
    int endgame_weight;
    int white_king_square = NO_SQUARE;
    int black_king_square = NO_SQUARE;
    uint64_t white_attacks;
    uint64_t black_attacks;
    const EvalParams *params = current_eval_params();
    uint64_t pieces;
    int use_state;
    int mobility_raw;
    int pawn_structure_raw;
    int king_safety_raw;
    int piece_activity_raw;
    int threats_raw;
    int space_raw;
    int advanced_raw;
    int stockfish_raw;
    int stockfish_replace;
    StockfishClassicalBreakdown stockfish_breakdown = {};

    if (position == 0) {
        return 0;
    }

    if (classical_fast_path_enabled()) {
        return evaluate_legacy_position_with_trace(position, state, trace);
    }

    use_state = state != 0 && state->valid &&
        state->params_generation == eval_params_generation();

    if (use_state) {
        material_score = state->material_score;
        piece_square_score_mg = state->piece_square_score_mg;
        piece_square_score_eg = state->piece_square_score_eg;
        phase = state->phase;
        white_king_square = position->white_king_square;
        black_king_square = position->black_king_square;
        white_attacks = state->attack_maps[COLOR_WHITE];
        black_attacks = state->attack_maps[COLOR_BLACK];
    } else {
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
                piece_square_score_mg += sign *
                    piece_square_value(piece, square, 0);
                piece_square_score_eg += sign *
                    piece_square_value(piece, square, 256);
            }
        }

        white_attacks = position_attack_map(position, COLOR_WHITE);
        black_attacks = position_attack_map(position, COLOR_BLACK);
    }

    endgame_weight = (params->max_phase - phase) * 256 / params->max_phase;
    if (endgame_weight < 0) {
        endgame_weight = 0;
    } else if (endgame_weight > 256) {
        endgame_weight = 256;
    }

    if (is_valid_square(white_king_square)) {
        piece_square_score_mg += piece_square_value(
            PIECE_WHITE_KING,
            white_king_square,
            0
        );
        piece_square_score_eg += piece_square_value(
            PIECE_WHITE_KING,
            white_king_square,
            256
        );
    }
    if (is_valid_square(black_king_square)) {
        piece_square_score_mg -= piece_square_value(
            PIECE_BLACK_KING,
            black_king_square,
            0
        );
        piece_square_score_eg -= piece_square_value(
            PIECE_BLACK_KING,
            black_king_square,
            256
        );
    }

    piece_square_score = (
        piece_square_score_mg * (256 - endgame_weight) +
        piece_square_score_eg * endgame_weight
    ) / 256;

    score = eval_scale_score(
        material_score,
        phase_scale_for_term(EVAL_FEATURE_MATERIAL, endgame_weight)
    );
    score += eval_scale_score(
        piece_square_score,
        phase_scale_for_term(EVAL_FEATURE_PIECE_SQUARE, endgame_weight)
    );

    mobility_raw = mobility_score_with_piece_attacks(
        position,
        use_state ? state->piece_attacks : 0
    );
    pawn_structure_raw = pawn_structure_score(position);
    king_safety_raw = king_safety_score_with_attacks(
        position,
        endgame_weight,
        white_attacks,
        black_attacks
    );
    piece_activity_raw = piece_activity_score_with_piece_attacks(
        position,
        endgame_weight,
        use_state ? state->piece_attacks : 0
    );
    threats_raw = threat_score_with_attacks(
        position,
        white_attacks,
        black_attacks
    );
    space_raw = space_score(position);
    stockfish_replace = params->stockfish_classical_replace != 0;
    advanced_raw = advanced_classical_score(
        position,
        use_state ? state : 0
    );
    stockfish_raw = stockfish_classical_score_with_breakdown(
        position,
        use_state ? state : 0,
        &stockfish_breakdown
    );

    if (trace != 0) {
        trace->material_and_piece_square = score;
        trace->mobility = stockfish_replace ? 0 : eval_scale_score(
            mobility_raw,
            phase_scale_for_term(EVAL_FEATURE_MOBILITY, endgame_weight));
        trace->pawn_structure = stockfish_replace ? 0 : eval_scale_score(
            pawn_structure_raw,
            phase_scale_for_term(
                EVAL_FEATURE_PAWN_STRUCTURE, endgame_weight));
        trace->king_safety = stockfish_replace ? 0 : eval_scale_score(
            king_safety_raw,
            phase_scale_for_term(EVAL_FEATURE_KING_SAFETY, endgame_weight));
        trace->piece_activity = stockfish_replace ? 0 : eval_scale_score(
            piece_activity_raw,
            phase_scale_for_term(
                EVAL_FEATURE_PIECE_ACTIVITY, endgame_weight));
        trace->threats = stockfish_replace ? 0 : eval_scale_score(
            threats_raw,
            phase_scale_for_term(EVAL_FEATURE_THREATS, endgame_weight));
        trace->space = stockfish_replace ? 0 : eval_scale_score(
            space_raw,
            phase_scale_for_term(EVAL_FEATURE_SPACE, endgame_weight));
        trace->advanced = stockfish_replace ? 0 : advanced_raw;
        trace->stockfish_classical = stockfish_raw;
        trace->stockfish_piece = stockfish_breakdown.piece;
        trace->stockfish_mobility = stockfish_breakdown.mobility;
        trace->stockfish_king = stockfish_breakdown.king;
        trace->stockfish_threats = stockfish_breakdown.threats;
        trace->stockfish_passed = stockfish_breakdown.passed;
        trace->stockfish_space = stockfish_breakdown.space;
        score += trace->mobility + trace->pawn_structure +
            trace->king_safety + trace->piece_activity + trace->threats +
            trace->space + trace->advanced + trace->stockfish_classical;
    } else {
        if (!stockfish_replace) {
            score += eval_scale_score(
                mobility_raw,
                phase_scale_for_term(EVAL_FEATURE_MOBILITY, endgame_weight));
            score += eval_scale_score(pawn_structure_raw,
                phase_scale_for_term(
                    EVAL_FEATURE_PAWN_STRUCTURE, endgame_weight));
            score += eval_scale_score(
                king_safety_raw,
                phase_scale_for_term(
                    EVAL_FEATURE_KING_SAFETY, endgame_weight));
            score += eval_scale_score(
                piece_activity_raw,
                phase_scale_for_term(
                    EVAL_FEATURE_PIECE_ACTIVITY, endgame_weight));
            score += eval_scale_score(
                threats_raw,
                phase_scale_for_term(EVAL_FEATURE_THREATS, endgame_weight));
            score += eval_scale_score(
                space_raw,
                phase_scale_for_term(EVAL_FEATURE_SPACE, endgame_weight));
            score += advanced_raw;
        }
        score += stockfish_raw;
    }

    if (trace != 0) {
        trace->tempo = position->side_to_move == COLOR_WHITE
            ? phase_scale_for_term(EVAL_FEATURE_TEMPO, endgame_weight)
            : -phase_scale_for_term(EVAL_FEATURE_TEMPO, endgame_weight);
        score += trace->tempo;
    } else {
        if (position->side_to_move == COLOR_WHITE) {
            score += phase_scale_for_term(EVAL_FEATURE_TEMPO, endgame_weight);
        } else if (position->side_to_move == COLOR_BLACK) {
            score -= phase_scale_for_term(EVAL_FEATURE_TEMPO, endgame_weight);
        }
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
            trace->advanced = -trace->advanced;
            trace->stockfish_classical = -trace->stockfish_classical;
            trace->stockfish_piece = -trace->stockfish_piece;
            trace->stockfish_mobility = -trace->stockfish_mobility;
            trace->stockfish_king = -trace->stockfish_king;
            trace->stockfish_threats = -trace->stockfish_threats;
            trace->stockfish_passed = -trace->stockfish_passed;
            trace->stockfish_space = -trace->stockfish_space;
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
                &trace->tempo,
                &trace->advanced,
                &trace->stockfish_classical
            };
            int term_total = 0;
            int index;

            for (index = 0; index < 10; ++index) {
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

static void set_nnue_trace(EvaluationTrace *trace, int score) {
    if (trace == 0) {
        return;
    }

    trace->material_and_piece_square = score;
    trace->mobility = 0;
    trace->pawn_structure = 0;
    trace->king_safety = 0;
    trace->piece_activity = 0;
    trace->threats = 0;
    trace->space = 0;
    trace->tempo = 0;
    trace->advanced = 0;
    trace->stockfish_classical = 0;
    trace->stockfish_piece = 0;
    trace->stockfish_mobility = 0;
    trace->stockfish_king = 0;
    trace->stockfish_threats = 0;
    trace->stockfish_passed = 0;
    trace->stockfish_space = 0;
    trace->total = score;
}

int evaluate_position_with_trace(
    const Position *position,
    EvaluationTrace *trace
) {
    int nnue_score;

    if (position == 0) {
        return 0;
    }

    /* Keep the classical path free of the NNUE evaluator call.  This is a
     * hot function in HCE mode, where the call can never succeed. */
    if (nnue_is_enabled() && nnue_evaluate(position, &nnue_score)) {
        set_nnue_trace(trace, nnue_score);
        return nnue_score;
    }

    return evaluate_classical_position_with_state(position, 0, trace);
}

int evaluate_position_with_state(
    const Position *position,
    const ClassicalEvalState *state,
    EvaluationTrace *trace
) {
    uint64_t key;
    EvaluationCacheEntry *entry;
    unsigned int current_nnue_generation;
    int score;

    if (position == 0) {
        return 0;
    }

    /* The search only enables this path when NNUE is disabled.  Retain the
     * public evaluator semantics if a caller violates that assumption. */
    if (nnue_is_enabled()) {
        return evaluate_position_with_trace(position, trace);
    }

    key = position_key(position);
    current_nnue_generation = nnue_generation();
    entry = &evaluation_cache[key & (EVALUATION_CACHE_SIZE - 1)];
    score = evaluate_classical_position_with_state(position, state, trace);
    entry->key = key;
    entry->params_generation = eval_params_generation();
    entry->nnue_generation = current_nnue_generation;
    entry->score = score;
    entry->valid = 1;
    return score;
}

static int classical_piece_phase(Piece piece) {
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

static uint64_t classical_pawn_attack_mask(int square, Color color) {
    int row;
    int column;
    int row_step;
    uint64_t attacks = 0;

    if (!is_valid_square(square) || color == COLOR_NONE) {
        return 0;
    }

    row = square_row(square);
    column = square_column(square);
    row_step = color == COLOR_WHITE ? -1 : 1;
    row += row_step;
    if (is_valid_coordinate(row, column - 1)) {
        attacks |= UINT64_C(1) << make_square(row, column - 1);
    }
    if (is_valid_coordinate(row, column + 1)) {
        attacks |= UINT64_C(1) << make_square(row, column + 1);
    }
    return attacks;
}

static uint64_t classical_piece_attack_mask(
    const Position *position,
    int square,
    Piece piece
) {
    PieceType type = piece_type(piece);

    if (position == 0 || piece == PIECE_NONE ||
        !is_valid_square(square)) {
        return 0;
    }

    if (type == PIECE_TYPE_PAWN) {
        return classical_pawn_attack_mask(square, piece_color(piece));
    }
    if (type == PIECE_TYPE_KNIGHT || type == PIECE_TYPE_KING ||
        type == PIECE_TYPE_BISHOP || type == PIECE_TYPE_ROOK ||
        type == PIECE_TYPE_QUEEN) {
        return position_piece_attack_map(position, square, type);
    }
    return 0;
}

static void classical_adjust_base_piece(
    ClassicalEvalState *state,
    Piece piece,
    int square,
    int direction
) {
    int color_sign;

    if (state == 0 || piece == PIECE_NONE ||
        piece_color(piece) == COLOR_NONE || !is_valid_square(square)) {
        return;
    }

    color_sign = piece_color(piece) == COLOR_WHITE ? 1 : -1;
    state->material_score += direction * color_sign * piece_value(piece);
    if (piece_type(piece) != PIECE_TYPE_KING) {
        state->piece_square_score_mg += direction * color_sign *
            piece_square_value(piece, square, 0);
        state->piece_square_score_eg += direction * color_sign *
            piece_square_value(piece, square, 256);
    }
    state->phase += direction * classical_piece_phase(piece);
}

static void classical_adjust_attack_counts(
    ClassicalEvalState *state,
    Color color,
    uint64_t attacks,
    int direction
) {
    if (state == 0 || color == COLOR_NONE) {
        return;
    }

    while (attacks != 0) {
        int square = __builtin_ctzll(attacks);

        attacks &= attacks - 1;
        if (direction > 0) {
            if (state->attack_counts[color][square] == 0) {
                state->attack_maps[color] |= UINT64_C(1) << square;
            }
            if (state->attack_counts[color][square] != 255) {
                state->attack_counts[color][square]++;
            }
        } else if (state->attack_counts[color][square] > 0) {
            if (state->attack_counts[color][square] == 1) {
                state->attack_maps[color] &= ~(UINT64_C(1) << square);
            }
            state->attack_counts[color][square]--;
        }
    }
}

static Piece classical_old_piece_at(
    const Position *position,
    const Move *move,
    const UndoState *undo,
    int square
) {
    int castle_rook_from = NO_SQUARE;
    int castle_rook_to = NO_SQUARE;
    Piece rook = PIECE_NONE;

    if (position == 0 || move == 0 || undo == 0 ||
        !is_valid_square(square)) {
        return PIECE_NONE;
    }

    if ((move->flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0 ||
        (move->flags & MOVE_FLAG_CASTLE_QUEENSIDE) != 0) {
        int row = square_row(move->from);
        int kingside = (move->flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0;

        castle_rook_from = make_square(row, kingside ? 7 : 0);
        castle_rook_to = make_square(row, kingside ? 5 : 3);
        rook = piece_color(undo->moved_piece) == COLOR_WHITE
            ? PIECE_WHITE_ROOK
            : PIECE_BLACK_ROOK;
    }

    if (square == castle_rook_from) {
        return rook;
    }
    if (square == castle_rook_to) {
        return PIECE_NONE;
    }
    if (square == move->from) {
        return undo->moved_piece;
    }
    if (square == move->to) {
        return undo->captured_square == move->to
            ? undo->captured_piece
            : PIECE_NONE;
    }
    if (square == undo->captured_square) {
        return undo->captured_piece;
    }
    return position_piece_at(position, square);
}

static void classical_set_piece_attack(
    ClassicalEvalState *state,
    const Position *position,
    const Move *move,
    const UndoState *undo,
    int square
) {
    Piece old_piece;
    Piece new_piece;
    uint64_t new_attacks = 0;

    if (state == 0 || position == 0 || move == 0 || undo == 0 ||
        !is_valid_square(square)) {
        return;
    }

    old_piece = classical_old_piece_at(position, move, undo, square);
    new_piece = position_piece_at(position, square);
    if (old_piece != PIECE_NONE) {
        classical_adjust_attack_counts(
            state,
            piece_color(old_piece),
            state->piece_attacks[square],
            -1
        );
    }

    if (new_piece != PIECE_NONE) {
        new_attacks = classical_piece_attack_mask(
            position,
            square,
            new_piece
        );
        classical_adjust_attack_counts(
            state,
            piece_color(new_piece),
            new_attacks,
            1
        );
    }
    state->piece_attacks[square] = new_attacks;
}

static int classical_piece_slides_in_direction(
    Piece piece,
    int row_step,
    int column_step
) {
    PieceType type = piece_type(piece);

    if (type == PIECE_TYPE_QUEEN) {
        return 1;
    }
    if (row_step != 0 && column_step != 0) {
        return type == PIECE_TYPE_BISHOP;
    }
    if (row_step != 0 || column_step != 0) {
        return type == PIECE_TYPE_ROOK;
    }
    return 0;
}

static void classical_mark_ray_slider(
    const Position *position,
    int source_square,
    int row_step,
    int column_step,
    uint64_t *affected
) {
    int row;
    int column;

    if (position == 0 || affected == 0 ||
        !is_valid_square(source_square)) {
        return;
    }

    row = square_row(source_square) + row_step;
    column = square_column(source_square) + column_step;
    while (is_valid_coordinate(row, column)) {
        int square = make_square(row, column);
        Piece piece = position_piece_at(position, square);

        if (piece != PIECE_NONE) {
            if (classical_piece_slides_in_direction(
                    piece,
                    row_step,
                    column_step
                )) {
                *affected |= UINT64_C(1) << square;
            }
            return;
        }
        row += row_step;
        column += column_step;
    }
}

static void classical_mark_old_ray_slider(
    const Position *position,
    const Move *move,
    const UndoState *undo,
    int source_square,
    int row_step,
    int column_step,
    uint64_t *affected
) {
    int row;
    int column;

    if (position == 0 || move == 0 || undo == 0 || affected == 0 ||
        !is_valid_square(source_square)) {
        return;
    }

    row = square_row(source_square) + row_step;
    column = square_column(source_square) + column_step;
    while (is_valid_coordinate(row, column)) {
        int square = make_square(row, column);
        Piece piece = classical_old_piece_at(
            position,
            move,
            undo,
            square
        );

        if (piece != PIECE_NONE) {
            if (classical_piece_slides_in_direction(
                    piece,
                    row_step,
                    column_step
                )) {
                *affected |= UINT64_C(1) << square;
            }
            return;
        }
        row += row_step;
        column += column_step;
    }
}

static void classical_mark_affected_sliders(
    const Position *position,
    const Move *move,
    const UndoState *undo,
    uint64_t changed,
    uint64_t *affected
) {
    static const int DIRECTIONS[8][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    uint64_t squares = changed;
    int direction;

    if (position == 0 || move == 0 || undo == 0 || affected == 0) {
        return;
    }

    while (squares != 0) {
        int square = __builtin_ctzll(squares);

        squares &= squares - 1;
        for (direction = 0; direction < 8; ++direction) {
            classical_mark_old_ray_slider(
                position,
                move,
                undo,
                square,
                DIRECTIONS[direction][0],
                DIRECTIONS[direction][1],
                affected
            );
            classical_mark_ray_slider(
                position,
                square,
                DIRECTIONS[direction][0],
                DIRECTIONS[direction][1],
                affected
            );
        }
    }
}

int classical_eval_state_build(
    const Position *position,
    ClassicalEvalState *state
) {
    uint64_t pieces;

    if (position == 0 || state == 0) {
        return 0;
    }

    memset(state, 0, sizeof(*state));
    state->params_generation = eval_params_generation();
    pieces = position->occupied;
    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        Piece piece = position_piece_at(position, square);
        uint64_t attacks;

        pieces &= pieces - 1;
        classical_adjust_base_piece(state, piece, square, 1);
        attacks = classical_piece_attack_mask(position, square, piece);
        state->piece_attacks[square] = attacks;
        classical_adjust_attack_counts(
            state,
            piece_color(piece),
            attacks,
            1
        );
    }
    state->valid = 1;
    return 1;
}

int classical_eval_state_update(
    const Position *position,
    const struct Move *move,
    const struct UndoState *undo,
    const ClassicalEvalState *parent,
    ClassicalEvalState *child
) {
    uint64_t changed = 0;
    uint64_t affected;

    if (position == 0 || move == 0 || undo == 0 ||
        parent == 0 || child == 0) {
        return 0;
    }

    if (!parent->valid ||
        parent->params_generation != eval_params_generation()) {
        return classical_eval_state_build(position, child);
    }

    *child = *parent;
    child->params_generation = eval_params_generation();

    if (is_valid_square(move->from)) {
        changed |= UINT64_C(1) << move->from;
    }
    if (is_valid_square(move->to)) {
        changed |= UINT64_C(1) << move->to;
    }
    if (is_valid_square(undo->captured_square)) {
        changed |= UINT64_C(1) << undo->captured_square;
    }
    if ((move->flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0 ||
        (move->flags & MOVE_FLAG_CASTLE_QUEENSIDE) != 0) {
        int row = square_row(move->from);
        int kingside = (move->flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0;

        changed |= UINT64_C(1) << make_square(row, kingside ? 7 : 0);
        changed |= UINT64_C(1) << make_square(row, kingside ? 5 : 3);
    }

    {
        uint64_t squares = changed;

        while (squares != 0) {
            int square = __builtin_ctzll(squares);
            Piece old_piece;
            Piece new_piece;

            squares &= squares - 1;
            old_piece = classical_old_piece_at(
                position,
                move,
                undo,
                square
            );
            new_piece = position_piece_at(position, square);
            if (old_piece != new_piece) {
                classical_adjust_base_piece(child, old_piece, square, -1);
                classical_adjust_base_piece(child, new_piece, square, 1);
            }
        }
    }

    affected = changed;
    classical_mark_affected_sliders(
        position,
        move,
        undo,
        changed,
        &affected
    );
    {
        uint64_t squares = affected;

        while (squares != 0) {
            int square = __builtin_ctzll(squares);

            squares &= squares - 1;
            classical_set_piece_attack(
                child,
                position,
                move,
                undo,
                square
            );
        }
    }

    child->valid = 1;
    return 1;
}

int evaluation_cache_probe(const Position *position, int *score) {
    uint64_t key;
    EvaluationCacheEntry *entry;
    unsigned int current_nnue_generation;

    if (position == 0 || score == 0) {
        return 0;
    }

    key = position_key(position);
    current_nnue_generation = nnue_generation();
    entry = &evaluation_cache[key & (EVALUATION_CACHE_SIZE - 1)];
    if (!entry->valid || entry->key != key ||
        entry->params_generation != eval_params_generation() ||
        entry->nnue_generation != current_nnue_generation) {
        return 0;
    }

    *score = entry->score;
    return 1;
}

int evaluate_position(const Position *position) {
    uint64_t key;
    EvaluationCacheEntry *entry;
    int score;
    unsigned int current_nnue_generation;

    if (position == 0) {
        return 0;
    }

    if (evaluation_cache_probe(position, &score)) {
        return score;
    }

    key = position_key(position);
    current_nnue_generation = nnue_generation();
    entry = &evaluation_cache[key & (EVALUATION_CACHE_SIZE - 1)];
    score = evaluate_position_with_trace(position, 0);
    entry->key = key;
    entry->params_generation = eval_params_generation();
    entry->nnue_generation = current_nnue_generation;
    entry->score = score;
    entry->valid = 1;
    return score;
}
