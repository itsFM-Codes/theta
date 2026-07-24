#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "move.h"

void generate_moves(const Position *position, MoveList *moves);
void generate_tactical_moves(const Position *position, MoveList *moves);
void generate_legal_moves(Position *position, MoveList *moves);
int make_legal_move(Position *position, Move move, UndoState *undo);

int find_king(const Position *position, Color color);
int is_square_attacked(
    const Position *position,
    int square,
    Color attacking_color
);

#endif // MOVEGEN_H
