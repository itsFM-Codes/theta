#ifndef SEARCH_H
#define SEARCH_H

#include "src/chess/move.h"
#include "src/chess/position.h"

#define SEARCH_INFINITY 30000
#define SEARCH_CHECKMATE 29000
#define MAX_PRINCIPAL_VARIATION 64

typedef struct PrincipalVariation {
    Move moves[MAX_PRINCIPAL_VARIATION];
    int count;
} PrincipalVariation;

int search_position(Position *position, int depth, Move *best_move);
int search_iterative(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth
);

#endif // SEARCH_H
