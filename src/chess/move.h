#ifndef MOVE_H
#define MOVE_H

#include "board.h"
#include "position.h"

#define MAX_MOVES 512

typedef enum MoveFlag {
    MOVE_NORMAL,
    MOVE_CAPTURE,
    MOVE_DOUBLE_PAWN,
    MOVE_EN_PASSANT,
    MOVE_CASTLE_KINGSIDE,
    MOVE_CASTLE_QUEENSIDE,
    MOVE_PROMOTION_QUEEN,
    MOVE_PROMOTION_ROOK,
    MOVE_PROMOTION_BISHOP,
    MOVE_PROMOTION_KNIGHT
} MoveFlag;

typedef struct Move {
    int from;
    int to;
    Piece promotion;
    MoveFlag flag;
} Move;

typedef struct MoveList {
    Move moves[MAX_MOVES];
    int count;
} MoveList;

typedef struct UndoState {
    Piece captured_piece;
    int castling_rights;
    int en_passant_square;
    int halfmove_clock;
} UndoState;

void generate_moves(const Position *position, MoveList *moves);
int make_move(Position *position, Move move, UndoState *undo);
void undo_move(Position *position, Move move, const UndoState *undo);

#endif // MOVE_H