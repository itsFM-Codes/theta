#ifndef STRATEGIC_H
#define STRATEGIC_H

#include "src/chess/position.h"

int threat_score(const Position *position);
int threat_score_with_attacks(
    const Position *position,
    uint64_t white_attacks,
    uint64_t black_attacks
);
int space_score(const Position *position);

#endif // STRATEGIC_H
