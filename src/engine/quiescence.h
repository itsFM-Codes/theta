#ifndef QUIESCENCE_H
#define QUIESCENCE_H

#include "search_internal.h"

int quiescence_search(
    Position *position,
    int alpha,
    int beta,
    int ply,
    SearchContext *context
);

#endif // QUIESCENCE_H
