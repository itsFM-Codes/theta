#include "move_ordering.h"

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

#define CHECK_MOVE_SCORE 5000
#define KILLER_MOVE_SCORE 4000
#define MAX_HISTORY_SCORE 3000

static int moves_are_equal(Move first, Move second) {
    return first.from == second.from &&
           first.to == second.to &&
           first.promotion == second.promotion &&
           first.flags == second.flags;
}

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
        case PIECE_TYPE_KING:
            return KING_VALUE;
        case PIECE_TYPE_NONE:
            return 0;
    }

    return 0;
}

static int move_gives_check(Position *position, Move move) {
    UndoState undo;
    int gives_check;

    if (!make_move(position, move, &undo)) {
        return 0;
    }

    gives_check = position_is_in_check(position);
    undo_move(position, move, &undo);
    return gives_check;
}

static int killer_move_score(
    const SearchContext *context,
    int ply,
    Move move
) {
    if (context == 0 || ply < 0 || ply >= MAX_KILLER_PLY) {
        return 0;
    }

    if (moves_are_equal(move, context->killer_moves[ply][0])) {
        return KILLER_MOVE_SCORE;
    }

    if (moves_are_equal(move, context->killer_moves[ply][1])) {
        return KILLER_MOVE_SCORE - 1;
    }

    return 0;
}

static int history_move_score(
    const SearchContext *context,
    Color color,
    Move move
) {
    if (context == 0 || color == COLOR_NONE) {
        return 0;
    }

    return context->history[color][move.from][move.to];
}

static int move_order_score(
    Position *position,
    Move move,
    int order_checks,
    const SearchContext *context,
    int ply
) {
    Piece attacker;
    Piece victim;

    if ((move.flags & MOVE_FLAG_CAPTURE) == 0) {
        int killer_score;

        if (order_checks && move_gives_check(position, move)) {
            return CHECK_MOVE_SCORE;
        }

        killer_score = killer_move_score(context, ply, move);
        if (killer_score > 0) {
            return killer_score;
        }

        return history_move_score(context, position->side_to_move, move);
    }

    attacker = position_piece_at(position, move.from);

    if (move.flags & MOVE_FLAG_EN_PASSANT) {
        victim = PIECE_WHITE_PAWN;
    } else {
        victim = position_piece_at(position, move.to);
    }

    return 10000 + piece_value(victim) * 16 - piece_value(attacker);
}

void order_moves(
    Position *position,
    MoveList *moves,
    int order_checks,
    const SearchContext *context,
    int ply
) {
    int index;
    int scores[MAX_MOVES];

    if (position == 0 || moves == 0) {
        return;
    }

    for (index = 0; index < moves->count; ++index) {
        scores[index] = move_order_score(
            position,
            moves->moves[index],
            order_checks,
            context,
            ply
        );
    }

    for (index = 0; index < moves->count; ++index) {
        int best_index = index;
        int next;

        for (next = index + 1; next < moves->count; ++next) {
            if (scores[next] > scores[best_index]) {
                best_index = next;
            }
        }

        if (best_index != index) {
            Move move = moves->moves[index];
            int score = scores[index];

            moves->moves[index] = moves->moves[best_index];
            moves->moves[best_index] = move;
            scores[index] = scores[best_index];
            scores[best_index] = score;
        }
    }
}

void record_quiet_cutoff(
    SearchContext *context,
    Color color,
    int ply,
    int depth,
    Move move
) {
    int bonus;
    int *history_score;

    if (context == 0 || ply < 0 || ply >= MAX_KILLER_PLY ||
        (move.flags & MOVE_FLAG_CAPTURE) != 0) {
        return;
    }

    if (!moves_are_equal(move, context->killer_moves[ply][0])) {
        context->killer_moves[ply][1] = context->killer_moves[ply][0];
        context->killer_moves[ply][0] = move;
    }

    if (color == COLOR_NONE) {
        return;
    }

    bonus = depth * depth;
    history_score = &context->history[color][move.from][move.to];
    *history_score += bonus;

    if (*history_score > MAX_HISTORY_SCORE) {
        *history_score = MAX_HISTORY_SCORE;
    }
}
