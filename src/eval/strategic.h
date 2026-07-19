#ifndef STRATEGIC_H
#define STRATEGIC_H

#include "src/chess/position.h"

int threat_score(const Position *position);
int space_score(const Position *position);

#endif // STRATEGIC_H
