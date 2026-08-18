#ifndef MOBILITY_H
#define MOBILITY_H

#include "src/chess/position.h"

int mobility_score(const Position *position);
int mobility_score_with_piece_attacks(
    const Position *position,
    const uint64_t *piece_attacks
);
#endif // MOBILITY_H
