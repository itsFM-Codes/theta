#ifndef ZOBRIST_H
#define ZOBRIST_H

#include <stdint.h>

#include "position.h"

uint64_t position_key(const Position *position);

#endif // ZOBRIST_H
