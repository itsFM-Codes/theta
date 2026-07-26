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
    int total;
} EvaluationTrace;

int evaluate_position(const Position *position);
int evaluate_position_with_trace(
    const Position *position,
    EvaluationTrace *trace
);

#endif // EVALUATION_H
