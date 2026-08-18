#ifndef EVAL_PARAMS_H
#define EVAL_PARAMS_H

#include "src/chess/board.h"

#define EVAL_PHASE_TERM_COUNT 9

typedef struct EvalParams {
    int piece_values[PIECE_TYPE_KING + 1];
    int max_phase;

    int material_scale;
    int piece_square_scale;
    int mobility_scale;
    int pawn_structure_scale;
    int king_safety_scale;
    int piece_activity_scale;
    int threat_scale;
    int space_scale;
    int tempo_bonus;

    int knight_mobility_weight;
    int bishop_mobility_weight;
    int rook_mobility_weight;
    int queen_mobility_weight;

    int pawn_table[SQUARE_COUNT];
    int knight_table[SQUARE_COUNT];
    int bishop_table[SQUARE_COUNT];
    int rook_table[SQUARE_COUNT];
    int queen_table[SQUARE_COUNT];
    int king_middlegame_table[SQUARE_COUNT];
    int king_endgame_table[SQUARE_COUNT];

    int doubled_pawn_penalty;
    int isolated_pawn_penalty;
    int pawn_island_penalty;
    int backward_pawn_penalty;
    int candidate_passed_pawn_bonus;
    int supported_passed_pawn_bonus;
    int connected_passed_pawn_bonus;
    int blocked_passed_pawn_penalty;
    int passed_pawn_king_distance_scale;

    int pawn_shield_bonus;
    int semi_open_file_penalty;
    int open_file_penalty;
    int king_ring_attack_unit;
    int king_danger_quadratic_divisor;
    int max_king_danger;
    int rook_file_pressure;
    int queen_file_pressure;

    int bishop_pair_middlegame_bonus;
    int bishop_pair_endgame_bonus;
    int semi_open_rook_bonus;
    int open_rook_bonus;
    int rook_seventh_rank_bonus;
    int knight_outpost_bonus;
    int bad_bishop_pawn_penalty;
    int bad_bishop_mobility_penalty;
    int trapped_minor_penalty;

    int pawn_threat_base;
    int hanging_piece_divisor;
    int safe_space_bonus;

    // Independent middlegame/endgame scales for the top-level evaluator.
    // The legacy scalar fields above remain available to the existing tuning
    // tools; the phase scales are what the runtime evaluator uses.
    int phase_scales[EVAL_PHASE_TERM_COUNT][2];

    // Optional attack-aware HCE terms. They default to zero so new feature
    // code can be screened and tuned without changing the baseline.
    int advanced_safe_mobility_bonus;
    int advanced_coordination_bonus;
    int advanced_king_ring_bonus;
    int advanced_hanging_bonus;
    int advanced_passed_path_bonus;

    // Scale for the optional Stockfish-style classical term set. When any
    // per-component scale below is non-zero, those scales replace this
    // aggregate scale and allow the terms to be tuned independently.
    int stockfish_classical_scale;
    int stockfish_piece_scale;
    int stockfish_mobility_scale;
    int stockfish_king_scale;
    int stockfish_threat_scale;
    int stockfish_passed_scale;
    int stockfish_space_scale;
    int stockfish_classical_replace;

} EvalParams;

extern const EvalParams DEFAULT_EVAL_PARAMS;

const EvalParams *current_eval_params(void);
void set_current_eval_params(const EvalParams *params);
void reset_current_eval_params(void);
unsigned int eval_params_generation(void);
int eval_piece_type_value(PieceType type);
int eval_scale_score(int score, int scale);

#endif // EVAL_PARAMS_H
