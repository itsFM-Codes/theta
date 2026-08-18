#ifndef EVALUATION_H
#define EVALUATION_H

#include "src/chess/position.h"
#include "eval_params.h"

#define PAWN_VALUE eval_piece_type_value(PIECE_TYPE_PAWN)
#define KNIGHT_VALUE eval_piece_type_value(PIECE_TYPE_KNIGHT)
#define BISHOP_VALUE eval_piece_type_value(PIECE_TYPE_BISHOP)
#define ROOK_VALUE eval_piece_type_value(PIECE_TYPE_ROOK)
#define QUEEN_VALUE eval_piece_type_value(PIECE_TYPE_QUEEN)
#define KING_VALUE eval_piece_type_value(PIECE_TYPE_KING)

typedef struct EvaluationTrace {
    int material_and_piece_square;
    int mobility;
    int pawn_structure;
    int king_safety;
    int piece_activity;
    int threats;
    int space;
    int tempo;
    int advanced;
    int stockfish_classical;
    int stockfish_piece;
    int stockfish_mobility;
    int stockfish_king;
    int stockfish_threats;
    int stockfish_passed;
    int stockfish_space;
    int total;
} EvaluationTrace;

/*
 * Search-local classical evaluation state.  The evaluator used to rebuild
 * the complete attack map every time a leaf was visited.  Keeping the
 * per-piece attacks and their union incrementally lets the search reuse the
 * expensive geometric work across a make/unmake pair while retaining the
 * existing full evaluator as the correctness oracle.
 */
typedef struct ClassicalEvalState {
    int material_score;
    int piece_square_score_mg;
    int piece_square_score_eg;
    int phase;
    uint64_t piece_attacks[SQUARE_COUNT];
    unsigned char attack_counts[COLOR_NONE][SQUARE_COUNT];
    uint64_t attack_maps[COLOR_NONE];
    unsigned int params_generation;
    int valid;
} ClassicalEvalState;

int evaluate_position(const Position *position);
int evaluation_cache_probe(const Position *position, int *score);
int evaluate_position_with_trace(
    const Position *position,
    EvaluationTrace *trace
);
int evaluate_position_with_state(
    const Position *position,
    const ClassicalEvalState *state,
    EvaluationTrace *trace
);
int classical_eval_state_build(
    const Position *position,
    ClassicalEvalState *state
);
int classical_eval_state_update(
    const Position *position,
    const struct Move *move,
    const struct UndoState *undo,
    const ClassicalEvalState *parent,
    ClassicalEvalState *child
);

#endif // EVALUATION_H
