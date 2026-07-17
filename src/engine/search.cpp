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

static int position_is_in_check(const Position *position) {
    int king_square = find_king(position, position->side_to_move);

    return is_valid_square(king_square) &&
           is_square_attacked(
               position,
               king_square,
               opposite_color(position->side_to_move)
           );
}

static void clear_variation(PrincipalVariation *variation) {
    if (variation != 0) {
        variation->count = 0;
    }
}

static void update_variation(
    PrincipalVariation *variation,
    Move move,
    const PrincipalVariation *child_variation
) {
    int index;

    if (variation == 0) {
        return;
    }

    variation->moves[0] = move;
    variation->count = 1;

    if (child_variation == 0) {
        return;
    }

    for (index = 0;
         index < child_variation->count &&
         variation->count < MAX_PRINCIPAL_VARIATION;
         ++index) {
        variation->moves[variation->count] = child_variation->moves[index];
        variation->count++;
    }
}

static int quiescence(
    Position *position,
    int alpha,
    int beta,
    int ply
) {
    MoveList moves;
    int in_check;
    int index;

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

    order_moves(position, &moves);

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        UndoState undo;
        int score;

        if (!in_check && (move.flags & MOVE_FLAG_CAPTURE) == 0) {
            continue;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        score = -quiescence(position, -beta, -alpha, ply + 1);
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

static int negamax(
    Position *position,
    int depth,
    int alpha,
    int beta,
    int ply,
    PrincipalVariation *variation
) {
    MoveList moves;
    int index;

    clear_variation(variation);

    if (depth <= 0) {
        return quiescence(position, alpha, beta, ply);
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
        PrincipalVariation child_variation;
        UndoState undo;
        int score;

        if (!make_move(position, move, &undo)) {
            continue;
        }

        score = -negamax(
            position,
            depth - 1,
            -beta,
            -alpha,
            ply + 1,
            &child_variation
        );
        undo_move(position, move, &undo);

        if (score >= beta) {
            update_variation(variation, move, &child_variation);
            return beta;
        }

        if (score > alpha) {
            alpha = score;
            update_variation(variation, move, &child_variation);
        }
    }

    return alpha;
}

static int search_position_with_variation(
    Position *position,
    int depth,
    PrincipalVariation *variation
) {
    MoveList moves;
    int alpha = -SEARCH_INFINITY;
    int beta = SEARCH_INFINITY;
    int index;

    clear_variation(variation);

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
        PrincipalVariation child_variation;
        UndoState undo;
        int score;

        if (!make_move(position, move, &undo)) {
            continue;
        }

        score = -negamax(
            position,
            depth - 1,
            -beta,
            -alpha,
            1,
            &child_variation
        );
        undo_move(position, move, &undo);

        if (score > alpha) {
            alpha = score;
            update_variation(variation, move, &child_variation);
        }
    }

    return alpha;
}

int search_position(Position *position, int depth, Move *best_move) {
    PrincipalVariation variation;
    int score = search_position_with_variation(position, depth, &variation);

    if (best_move != 0) {
        best_move->from = NO_SQUARE;
        best_move->to = NO_SQUARE;
        best_move->promotion = PIECE_NONE;
        best_move->flags = MOVE_FLAG_NONE;

        if (variation.count > 0) {
            *best_move = variation.moves[0];
        }
    }

    return score;
}

int search_iterative(
    Position *position,
    int maximum_depth,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth
) {
    Move move;
    PrincipalVariation current_variation;
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

    clear_variation(variation);

    if (position == 0) {
        return 0;
    }

    if (maximum_depth <= 0) {
        return evaluate_position(position);
    }

    for (depth = 1; depth <= maximum_depth; ++depth) {
        score = search_position_with_variation(
            position,
            depth,
            &current_variation
        );

        if (current_variation.count > 0) {
            move = current_variation.moves[0];

            if (best_move != 0) {
                *best_move = move;
            }

            if (variation != 0) {
                *variation = current_variation;
            }
        }

        if (completed_depth != 0) {
            *completed_depth = depth;
        }
    }

    return score;
}
