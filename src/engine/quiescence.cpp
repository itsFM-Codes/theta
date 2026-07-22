#include "quiescence.h"

#include "move_ordering.h"
#include "static_exchange.h"

#include "src/chess/movegen.h"
#include "src/chess/zobrist.h"
#include "src/eval/evaluation.h"

#define DELTA_MARGIN 200
#define QUIESCENCE_CHECK_DEPTH 1

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

static int position_has_legal_move(Position *position, const MoveList *moves) {
    int index;

    for (index = 0; index < moves->count; ++index) {
        Move move = moves->moves[index];
        UndoState undo;

        if (make_legal_move(position, move, &undo)) {
            undo_move(position, move, &undo);
            return 1;
        }
    }

    return 0;
}

static int quiescence_search_internal(
    Position *position,
    int alpha,
    int beta,
    int ply,
    int quiescence_depth,
    SearchContext *context
) {
    MoveList moves;
    MovePicker move_picker;
    Move table_move;
    uint64_t key = 0;
    int table_depth = -quiescence_depth;
    int table_score;
    int stand_pat = 0;
    int in_check;
    int legal_move_count = 0;
    int original_alpha = alpha;
    int index;

    table_move.from = NO_SQUARE;
    table_move.to = NO_SQUARE;
    table_move.promotion = PIECE_NONE;
    table_move.flags = MOVE_FLAG_NONE;

    search_record_node(context, ply, 1);

    if (search_has_stopped(context)) {
        return 0;
    }

    if (search_is_draw(context, position)) {
        return search_draw_score(context);
    }

    if (ply >= MAX_SEARCH_PLY - 1) {
        return evaluate_position(position);
    }

    if (context != 0) {
        key = position_key(position);
        if (probe_search_transposition_table(
                context,
                key, table_depth, alpha, beta, ply, &table_score,
                &table_move
            )) {
            return table_score;
        }
    }

    in_check = position_is_in_check(position);
    generate_moves(position, &moves);

    if (!in_check && !position_has_legal_move(position, &moves)) {
        int score = search_draw_score(context);

        if (context != 0) {
            store_transposition_table(
                &context->shared_state->transposition_table,
                key, table_depth, search_score_to_table(score, ply),
                TRANSPOSITION_EXACT, table_move,
                &context->transposition_statistics
            );
        }
        return score;
    }

    if (!in_check) {
        stand_pat = evaluate_position(position);

        if (stand_pat >= beta) {
            if (context != 0) {
                store_transposition_table(
                    &context->shared_state->transposition_table,
                    key, table_depth, search_score_to_table(beta, ply),
                    TRANSPOSITION_LOWER_BOUND, table_move,
                    &context->transposition_statistics
                );
            }
            return beta;
        }

        if (stand_pat > alpha) {
            alpha = stand_pat;
        }
    }

    initialize_move_picker(
        &move_picker, position, &moves, 0, context, ply, &table_move
    );

    for (index = 0; move_picker_next(&move_picker, &moves.moves[index]);
         ++index) {
        Move move = moves.moves[index];
        UndoState undo;
        int tactical_move =
            (move.flags & (MOVE_FLAG_CAPTURE | MOVE_FLAG_PROMOTION)) != 0;
        int gives_check = 0;
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        if (!in_check &&
            (tactical_move || quiescence_depth < QUIESCENCE_CHECK_DEPTH)) {
            gives_check = move_gives_check(position, move);
        }

        if (!in_check && !tactical_move) {
            if (quiescence_depth >= QUIESCENCE_CHECK_DEPTH || !gives_check) {
                continue;
            }

            if (context != 0) {
                context->quiescence_check_moves++;
            }
        }

        if (!in_check && tactical_move &&
            (move.flags & MOVE_FLAG_PROMOTION) == 0 &&
            !gives_check &&
            stand_pat + capture_value(position, move) + DELTA_MARGIN < alpha) {
            if (context != 0) {
                context->delta_prunes++;
            }
            continue;
        }

        if (!in_check && tactical_move &&
            (move.flags & MOVE_FLAG_PROMOTION) == 0 &&
            !gives_check &&
            static_exchange_evaluation(position, move) < 0) {
            if (context != 0) {
                context->see_prunes++;
            }
            continue;
        }

        if (!make_legal_move(position, move, &undo)) {
            continue;
        }
        legal_move_count++;

        search_push_position(context, position);

        score = -quiescence_search_internal(
            position,
            -beta,
            -alpha,
            ply + 1,
            quiescence_depth + 1,
            context
        );
        search_pop_position(context);
        undo_move(position, move, &undo);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (score >= beta) {
            if (context != 0) {
                store_transposition_table(
                    &context->shared_state->transposition_table,
                    key, table_depth, search_score_to_table(beta, ply),
                    TRANSPOSITION_LOWER_BOUND, move,
                    &context->transposition_statistics
                );
            }
            return beta;
        }

        if (score > alpha) {
            alpha = score;
            table_move = move;
        }
    }

    if (in_check && legal_move_count == 0) {
        int score = -SEARCH_CHECKMATE + ply;

        if (context != 0) {
            store_transposition_table(
                &context->shared_state->transposition_table,
                key, table_depth, search_score_to_table(score, ply),
                TRANSPOSITION_EXACT, table_move,
                &context->transposition_statistics
            );
        }
        return score;
    }

    if (context != 0) {
        store_transposition_table(
            &context->shared_state->transposition_table,
            key, table_depth, search_score_to_table(alpha, ply),
            alpha <= original_alpha
                ? TRANSPOSITION_UPPER_BOUND
                : TRANSPOSITION_EXACT,
            table_move,
            &context->transposition_statistics
        );
    }
    return alpha;
}

int quiescence_search(
    Position *position,
    int alpha,
    int beta,
    int ply,
    SearchContext *context
) {
    return quiescence_search_internal(
        position,
        alpha,
        beta,
        ply,
        0,
        context
    );
}
