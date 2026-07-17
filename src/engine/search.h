#ifndef SEARCH_H
#define SEARCH_H

#include "src/chess/move.h"
#include "src/chess/position.h"

#define SEARCH_INFINITY 30000
#define SEARCH_CHECKMATE 29000

int search_position(Position *position, int depth, Move *best_move);
int search_iterative(
    Position *position,
    int maximum_depth,
    Move *best_move,
    int *completed_depth
);

#endif // SEARCH_H
