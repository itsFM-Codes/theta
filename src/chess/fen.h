#ifndef FEN_H
#define FEN_H

#include "position.h"

int position_from_fen(Position *position, const char *fen);
int position_to_fen(const Position *position, char *fen, int size);

#endif // FEN_H
