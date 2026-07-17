#ifndef PIECE_SQUARE_TABLES_H
#define PIECE_SQUARE_TABLES_H

#include "src/chess/board.h"

int piece_square_value(Piece piece, int square, int endgame_weight);

#endif // PIECE_SQUARE_TABLES_H
