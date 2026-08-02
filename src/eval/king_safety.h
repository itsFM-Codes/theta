#ifndef KING_SAFETY_H
#define KING_SAFETY_H

#include "src/chess/position.h"

int king_safety_score(const Position *position, int endgame_weight);
int king_safety_score_with_attacks(
    const Position *position,
    int endgame_weight,
    uint64_t white_attacks,
    uint64_t black_attacks
);

#endif // KING_SAFETY_H
