#include "quiescence.h"

#include "move_ordering.h"

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

int quiescence_search(
    Position *position,
    int alpha,
    int beta,
    int ply,
    SearchContext *context
) {
    MoveList moves;
    int in_check;
    int index;

    if (search_has_stopped(context)) {
        return 0;
    }

    generate_legal_moves(position, &moves);
    in_check = position_is_in_check(position);

    if (moves.count == 0) {
        if (in_check) {
            return -SEARCH_CHECKMATE + ply;
        }

        return 0;
    }

    if (!in_check) {
        int stand_pat = evaluate_position(position);

        if (stand_pat >= beta) {
            return beta;
        }

        if (stand_pat > alpha) {
            alpha = stand_pat;
        }
    }

    order_moves(position, &moves, 0, context, ply, 0);

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        UndoState undo;
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        if (!in_check && (move.flags & MOVE_FLAG_CAPTURE) == 0) {
            continue;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        score = -quiescence_search(position, -beta, -alpha, ply + 1, context);
        undo_move(position, move, &undo);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (score >= beta) {
            return beta;
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}
