#ifndef PIECE_ACTIVITY_H
#define PIECE_ACTIVITY_H

#include "src/chess/position.h"

int piece_activity_score(const Position *position, int endgame_weight);
int piece_activity_score_with_piece_attacks(
    const Position *position,
    int endgame_weight,
    const uint64_t *piece_attacks
);

#endif // PIECE_ACTIVITY_H
