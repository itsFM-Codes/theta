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

typedef void (*SearchInfoCallback)(
    int depth,
    int score,
    const PrincipalVariation *variation,
    void *user_data
);

int search_position(Position *position, int depth, Move *best_move);
int search_iterative(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth
);
int search_iterative_with_callback(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
);

#endif // SEARCH_H
