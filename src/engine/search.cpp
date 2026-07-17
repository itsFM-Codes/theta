#include "search.h"

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

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
