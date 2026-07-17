#include "search.h"

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

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

static int move_order_score(const Position *position, Move move) {
    Piece attacker;
    Piece victim;

    if ((move.flags & MOVE_FLAG_CAPTURE) == 0) {
        return 0;
    }

    attacker = position_piece_at(position, move.from);

    if (move.flags & MOVE_FLAG_EN_PASSANT) {
        victim = PIECE_WHITE_PAWN;
    } else {
        victim = position_piece_at(position, move.to);
    }

    return 10000 + piece_value(victim) * 16 - piece_value(attacker);
}

static void order_moves(const Position *position, MoveList *moves) {
    int index;

    if (position == 0 || moves == 0) {
        return;
    }

    for (index = 0; index < moves->count; ++index) {
        int best_index = index;
        int next;

        for (next = index + 1; next < moves->count; ++next) {
            if (move_order_score(position, moves->moves[next]) >
                move_order_score(position, moves->moves[best_index])) {
                best_index = next;
            }
        }

        if (best_index != index) {
            Move move = moves->moves[index];

            moves->moves[index] = moves->moves[best_index];
            moves->moves[best_index] = move;
        }
    }
}

static int negamax(
    Position *position,
    int depth,
    int alpha,
    int beta,
    int ply
) {
    MoveList moves;
    int index;

    if (depth <= 0) {
        return evaluate_position(position);
    }

    generate_legal_moves(position, &moves);
    order_moves(position, &moves);

    if (moves.count == 0) {
        int king_square = find_king(position, position->side_to_move);

        if (is_valid_square(king_square) &&
            is_square_attacked(
                position,
                king_square,
                opposite_color(position->side_to_move)
            )) {
            return -SEARCH_CHECKMATE + ply;
        }

        return 0;
    }

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        UndoState undo;
        int score;

        if (!make_move(position, move, &undo)) {
            continue;
        }

        score = -negamax(position, depth - 1, -beta, -alpha, ply + 1);
        undo_move(position, move, &undo);

        if (score >= beta) {
            return beta;
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

int search_position(Position *position, int depth, Move *best_move) {
    MoveList moves;
    int alpha = -SEARCH_INFINITY;
    int beta = SEARCH_INFINITY;
    int index;

    if (best_move != 0) {
        best_move->from = NO_SQUARE;
        best_move->to = NO_SQUARE;
        best_move->promotion = PIECE_NONE;
        best_move->flags = MOVE_FLAG_NONE;
    }

    if (position == 0) {
        return 0;
    }

    if (depth <= 0) {
        return evaluate_position(position);
    }

    generate_legal_moves(position, &moves);
    order_moves(position, &moves);

    if (moves.count == 0) {
        int king_square = find_king(position, position->side_to_move);

        if (is_valid_square(king_square) &&
            is_square_attacked(
                position,
                king_square,
                opposite_color(position->side_to_move)
            )) {
            return -SEARCH_CHECKMATE;
        }

        return 0;
    }

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        UndoState undo;
        int score;

        if (!make_move(position, move, &undo)) {
            continue;
        }

        score = -negamax(position, depth - 1, -beta, -alpha, 1);
        undo_move(position, move, &undo);

        if (score > alpha) {
            alpha = score;

            if (best_move != 0) {
                *best_move = move;
            }
        }
    }

    return alpha;
}

int search_iterative(
    Position *position,
    int maximum_depth,
    Move *best_move,
    int *completed_depth
) {
    Move move;
    int depth;
    int score = 0;

    if (best_move != 0) {
        best_move->from = NO_SQUARE;
        best_move->to = NO_SQUARE;
        best_move->promotion = PIECE_NONE;
        best_move->flags = MOVE_FLAG_NONE;
    }

    if (completed_depth != 0) {
        *completed_depth = 0;
    }

    if (position == 0) {
        return 0;
    }

    if (maximum_depth <= 0) {
        return evaluate_position(position);
    }

    for (depth = 1; depth <= maximum_depth; ++depth) {
        score = search_position(position, depth, &move);

        if (best_move != 0 &&
            is_valid_square(move.from) &&
            is_valid_square(move.to)) {
            *best_move = move;
        }

        if (completed_depth != 0) {
            *completed_depth = depth;
        }
    }

    return score;
}
