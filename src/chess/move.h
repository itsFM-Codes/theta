#ifndef MOVE_H
#define MOVE_H

#include <stdint.h>

#include "position.h"

#define MAX_MOVES 256

#define MOVE_FLAG_NONE              0
#define MOVE_FLAG_CAPTURE           (1 << 0)
#define MOVE_FLAG_DOUBLE_PAWN       (1 << 1)
#define MOVE_FLAG_EN_PASSANT        (1 << 2)
#define MOVE_FLAG_CASTLE_KINGSIDE   (1 << 3)
#define MOVE_FLAG_CASTLE_QUEENSIDE  (1 << 4)
#define MOVE_FLAG_PROMOTION         (1 << 5)

typedef struct Move {
    signed int from : 8;
    signed int to : 8;
    Piece promotion : 4;
    unsigned int flags : 8;
} Move;

typedef struct MoveList {
    Move moves[MAX_MOVES];
    int count;
} MoveList;

typedef struct UndoState {
    Piece moved_piece;
    Piece captured_piece;
    int captured_square;

    Color side_to_move;
    int castling_rights;
    int en_passant_square;
    int halfmove_clock;
    int fullmove_number;
    uint64_t zobrist_key;
    int zobrist_key_valid;
    Color zobrist_side_to_move;
    int zobrist_castling_rights;
    int zobrist_en_passant_square;
} UndoState;

int make_move(Position *position, Move move, UndoState *undo);
void undo_move(Position *position, Move move, const UndoState *undo);

#endif // MOVE_H
