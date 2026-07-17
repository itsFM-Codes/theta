#ifndef KING_SAFETY_H
#define KING_SAFETY_H

#include "src/chess/position.h"

int king_safety_score(const Position *position, int endgame_weight);

#endif // KING_SAFETY_H
