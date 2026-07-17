#include "static_exchange.h"

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

#define MAX_EXCHANGE_DEPTH 16

static int piece_value(Piece piece) {
    switch (piece_type(piece)) {
        case PIECE_TYPE_PAWN:
            return PAWN_VALUE;
        case PIECE_TYPE_KNIGHT:
            return KNIGHT_VALUE;
        case PIECE_TYPE_BISHOP:
            return BISHOP_VALUE;
        case PIECE_TYPE_ROOK:
            return ROOK_VALUE;
        case PIECE_TYPE_QUEEN:
            return QUEEN_VALUE;
        default:
            return 0;
    }
}

static int captured_piece_value(const Position *position, Move move) {
    if (move.flags & MOVE_FLAG_EN_PASSANT) {
        return PAWN_VALUE;
    }

    return piece_value(position_piece_at(position, move.to));
}

static int exchange_after_capture(Position *position, int square, int depth) {
    MoveList moves;
    int best_score = 0;
    int index;

    if (depth >= MAX_EXCHANGE_DEPTH) {
        return 0;
    }

    generate_legal_moves(position, &moves);

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        UndoState undo;
        int score;

        if (move.to != square || (move.flags & MOVE_FLAG_CAPTURE) == 0) {
            continue;
        }

        score = captured_piece_value(position, move);

        if (!make_move(position, move, &undo)) {
            continue;
        }

        score -= exchange_after_capture(position, square, depth + 1);
        undo_move(position, move, &undo);

        if (score > best_score) {
            best_score = score;
        }
    }

    return best_score;
}

int static_exchange_evaluation(Position *position, Move move) {
    UndoState undo;
    int score;

    if (position == 0 || (move.flags & MOVE_FLAG_CAPTURE) == 0) {
        return 0;
    }

    score = captured_piece_value(position, move);

    if (!make_move(position, move, &undo)) {
        return 0;
    }

    if (move.flags & MOVE_FLAG_PROMOTION) {
        score += piece_value(move.promotion) - PAWN_VALUE;
    }

    score -= exchange_after_capture(position, move.to, 0);
    undo_move(position, move, &undo);

    return score;
}
