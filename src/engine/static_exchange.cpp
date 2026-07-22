#include "static_exchange.h"

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

#define MAX_EXCHANGE_DEPTH 16

static int absolute_value(int value) {
    return value < 0 ? -value : value;
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

static int slider_path_is_clear(
    const Position *position,
    int from,
    int to,
    int row_step,
    int column_step
) {
    int row = square_row(from) + row_step;
    int column = square_column(from) + column_step;
    int target_row = square_row(to);
    int target_column = square_column(to);

    while (row != target_row || column != target_column) {
        if (!is_valid_coordinate(row, column) ||
            position_piece_at_coordinates(position, row, column) !=
                PIECE_NONE) {
            return 0;
        }
        row += row_step;
        column += column_step;
    }

    return 1;
}

static int piece_attacks_square(
    const Position *position,
    int from,
    int to
) {
    Piece piece = position_piece_at(position, from);
    PieceType type = piece_type(piece);
    int row_delta = square_row(to) - square_row(from);
    int column_delta = square_column(to) - square_column(from);
    int row_distance = absolute_value(row_delta);
    int column_distance = absolute_value(column_delta);
    int row_step = row_delta == 0 ? 0 : (row_delta > 0 ? 1 : -1);
    int column_step = column_delta == 0 ? 0 :
        (column_delta > 0 ? 1 : -1);

    switch (type) {
        case PIECE_TYPE_PAWN:
            return column_distance == 1 &&
                row_delta == (piece_color(piece) == COLOR_WHITE ? -1 : 1);
        case PIECE_TYPE_KNIGHT:
            return (row_distance == 2 && column_distance == 1) ||
                   (row_distance == 1 && column_distance == 2);
        case PIECE_TYPE_BISHOP:
            return row_distance == column_distance && row_distance > 0 &&
                slider_path_is_clear(
                    position, from, to, row_step, column_step
                );
        case PIECE_TYPE_ROOK:
            return ((row_delta == 0) != (column_delta == 0)) &&
                slider_path_is_clear(
                    position, from, to, row_step, column_step
                );
        case PIECE_TYPE_QUEEN:
            return ((row_distance == column_distance && row_distance > 0) ||
                    ((row_delta == 0) != (column_delta == 0))) &&
                slider_path_is_clear(
                    position, from, to, row_step, column_step
                );
        case PIECE_TYPE_KING:
            return row_distance <= 1 && column_distance <= 1 &&
                row_distance + column_distance > 0;
        case PIECE_TYPE_NONE:
            return 0;
    }

    return 0;
}

static Move exchange_capture_move(
    const Position *position,
    int from,
    int to
) {
    Move move;
    Piece attacker = position_piece_at(position, from);
    int target_row = square_row(to);

    move.from = from;
    move.to = to;
    move.flags = MOVE_FLAG_CAPTURE;
    move.promotion = PIECE_NONE;

    if (piece_type(attacker) == PIECE_TYPE_PAWN &&
        (target_row == 0 || target_row == BOARD_SIZE - 1)) {
        move.flags |= MOVE_FLAG_PROMOTION;
        move.promotion = piece_color(attacker) == COLOR_WHITE
            ? PIECE_WHITE_QUEEN
            : PIECE_BLACK_QUEEN;
    }

    return move;
}

static int exchange_after_capture(Position *position, int square, int depth) {
    int best_score = 0;
    int from;

    if (depth >= MAX_EXCHANGE_DEPTH) {
        return 0;
    }

    // Only captures onto this square matter, so find its attackers directly.
    for (from = 0; from < SQUARE_COUNT; ++from) {
        Piece attacker = position_piece_at(position, from);
        Move move;
        UndoState undo;
        int score;

        if (piece_color(attacker) != position->side_to_move ||
            !piece_attacks_square(position, from, square)) {
            continue;
        }

        move = exchange_capture_move(position, from, square);
        score = captured_piece_value(position, move);

        if (!make_legal_move(position, move, &undo)) {
            continue;
        }

        if (move.flags & MOVE_FLAG_PROMOTION) {
            score += piece_value(move.promotion) - PAWN_VALUE;
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
