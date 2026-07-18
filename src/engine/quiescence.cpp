#include "quiescence.h"

#include "move_ordering.h"
#include "static_exchange.h"

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

#define DELTA_MARGIN 200
#define QUIESCENCE_CHECK_PLY_LIMIT 2

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

static int capture_value(const Position *position, Move move) {
    if (move.flags & MOVE_FLAG_EN_PASSANT) {
        return PAWN_VALUE;
    }

    return piece_value(position_piece_at(position, move.to));
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

int quiescence_search(
    Position *position,
    int alpha,
    int beta,
    int ply,
    SearchContext *context
) {
    MoveList moves;
    int stand_pat = 0;
    int in_check;
    int index;

    search_record_node(context, ply, 1);

    if (search_has_stopped(context)) {
        return 0;
    }

    if (search_is_draw(context, position)) {
        return 0;
    }

    if (ply >= MAX_SEARCH_PLY - 1) {
        return evaluate_position(position);
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
        stand_pat = evaluate_position(position);

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
        int tactical_move =
            (move.flags & (MOVE_FLAG_CAPTURE | MOVE_FLAG_PROMOTION)) != 0;
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        if (!in_check && !tactical_move) {
            if (ply > QUIESCENCE_CHECK_PLY_LIMIT ||
                !move_gives_check(position, move)) {
                continue;
            }

            if (context != 0) {
                context->quiescence_check_moves++;
            }
        }

        if (!in_check && tactical_move &&
            (move.flags & MOVE_FLAG_PROMOTION) == 0 &&
            stand_pat + capture_value(position, move) + DELTA_MARGIN < alpha) {
            if (context != 0) {
                context->delta_prunes++;
            }
            continue;
        }

        if (!in_check && tactical_move &&
            (move.flags & MOVE_FLAG_PROMOTION) == 0 &&
            static_exchange_evaluation(position, move) < 0) {
            if (context != 0) {
                context->see_prunes++;
            }
            continue;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        search_push_position(context, position);

        score = -quiescence_search(position, -beta, -alpha, ply + 1, context);
        search_pop_position(context);
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
