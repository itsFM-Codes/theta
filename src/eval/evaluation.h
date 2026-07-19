#ifndef EVALUATION_H
#define EVALUATION_H

#include "src/chess/position.h"

#define PAWN_VALUE 100
#define KNIGHT_VALUE 320
#define BISHOP_VALUE 330
#define ROOK_VALUE 500
#define QUEEN_VALUE 900
#define KING_VALUE 0

typedef struct EvaluationTrace {
    int material_and_piece_square;
    int mobility;
    int pawn_structure;
    int king_safety;
    int piece_activity;
    int threats;
    int space;
    int total;
} EvaluationTrace;

int evaluate_position(const Position *position);
int evaluate_position_with_trace(
    const Position *position,
    EvaluationTrace *trace
);

#endif // EVALUATION_H
