#ifndef MOVE_ORDERING_H
#define MOVE_ORDERING_H

#include "search_internal.h"

void order_moves(
    Position *position,
    MoveList *moves,
    int order_checks,
    const SearchContext *context,
    int ply,
    const Move *table_move
);

void record_quiet_cutoff(
    SearchContext *context,
    Color color,
    int ply,
    int depth,
    Move move
);
void record_quiet_failures(
    SearchContext *context,
    Color color,
    int depth,
    const MoveList *moves,
    int count
);
void record_capture_cutoff(
    SearchContext *context,
    const Position *position,
    Color color,
    int depth,
    Move move
);
void record_capture_failures(
    SearchContext *context,
    const Position *position,
    Color color,
    int depth,
    const MoveList *moves,
    int count
);

#endif // MOVE_ORDERING_H
