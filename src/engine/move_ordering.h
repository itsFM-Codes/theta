#ifndef MOVE_ORDERING_H
#define MOVE_ORDERING_H

#include "search_internal.h"

void order_moves(
    Position *position,
    MoveList *moves,
    int order_checks,
    const SearchContext *context,
    int ply
);

void record_killer_move(SearchContext *context, int ply, Move move);

#endif // MOVE_ORDERING_H
