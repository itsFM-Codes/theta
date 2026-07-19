#ifndef PIECE_ACTIVITY_H
#define PIECE_ACTIVITY_H

#include "src/chess/position.h"

int piece_activity_score(const Position *position, int endgame_weight);

#endif // PIECE_ACTIVITY_H
