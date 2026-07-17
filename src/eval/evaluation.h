#ifndef EVALUATION_H
#define EVALUATION_H

#include "src/chess/position.h"

#define PAWN_VALUE 100
#define KNIGHT_VALUE 320
#define BISHOP_VALUE 330
#define ROOK_VALUE 500
#define QUEEN_VALUE 900
#define KING_VALUE 0

int evaluate_position(const Position *position);

#endif // EVALUATION_H
