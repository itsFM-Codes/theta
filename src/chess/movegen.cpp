#include "movegen.h"

static void add_move(MoveList *moves, int from, int to, Piece promotion, int flags) {
    Move *move;

    if (moves->count >= MAX_MOVES) {
        return;
    }

    move = &moves->moves[moves->count];
    move->from = from;
    move->to = to;
    move->promotion = promotion;
    move->flags = flags;
    moves->count++;
}

static int is_enemy_piece(const Position *position, int square) {
    Piece piece = position_piece_at(position, square);

    return piece != PIECE_NONE && piece_color(piece) != position->side_to_move;
}

static void add_promotion_moves(const Position *position, MoveList *moves, int from, int to, int flags) {
    if (position->side_to_move == COLOR_WHITE) {
        add_move(moves, from, to, PIECE_WHITE_QUEEN, flags);
        add_move(moves, from, to, PIECE_WHITE_ROOK, flags);
        add_move(moves, from, to, PIECE_WHITE_BISHOP, flags);
        add_move(moves, from, to, PIECE_WHITE_KNIGHT, flags);
    } else {
        add_move(moves, from, to, PIECE_BLACK_QUEEN, flags);
        add_move(moves, from, to, PIECE_BLACK_ROOK, flags);
        add_move(moves, from, to, PIECE_BLACK_BISHOP, flags);
        add_move(moves, from, to, PIECE_BLACK_KNIGHT, flags);
    }
}

static void generate_pawn_moves(const Position *position, MoveList *moves, int from) {
    int row = square_row(from);
    int column = square_column(from);
    int direction;
    int starting_row;
    int promotion_row;
    int next_row;
    int to;
    int capture_column;
    int capture_square;
    int offset;

    if (position->side_to_move == COLOR_WHITE) {
        direction = -1;
        starting_row = 6;
        promotion_row = 0;
    } else {
        direction = 1;
        starting_row = 1;
        promotion_row = 7;
    }

    next_row = row + direction;
    to = make_square(next_row, column);

    if (is_valid_square(to) && position_piece_at(position, to) == PIECE_NONE) {
        if (next_row == promotion_row) {
            add_promotion_moves(
                position,
                moves,
                from,
                to,
                MOVE_FLAG_PROMOTION
            );
        } else {
            add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_NONE);

            if (row == starting_row) {
                int double_to = make_square(row + direction * 2, column);

                if (position_piece_at(position, double_to) == PIECE_NONE) {
                    add_move(
                        moves,
                        from,
                        double_to,
                        PIECE_NONE,
                        MOVE_FLAG_DOUBLE_PAWN
                    );
                }
            }
        }
    }

    for (offset = -1; offset <= 1; offset += 2) {
        capture_column = column + offset;
        capture_square = make_square(next_row, capture_column);

        if (!is_valid_square(capture_square)) {
            continue;
        }

        if (is_enemy_piece(position, capture_square)) {
            if (next_row == promotion_row) {
                add_promotion_moves(
                    position,
                    moves,
                    from,
                    capture_square,
                    MOVE_FLAG_CAPTURE | MOVE_FLAG_PROMOTION
                );
            } else {
                add_move(
                    moves,
                    from,
                    capture_square,
                    PIECE_NONE,
                    MOVE_FLAG_CAPTURE
                );
            }
        } else if (capture_square == position->en_passant_square) {
            add_move(
                moves,
                from,
                capture_square,
                PIECE_NONE,
                MOVE_FLAG_CAPTURE | MOVE_FLAG_EN_PASSANT
            );
        }
    }
}

static void generate_knight_moves(const Position *position, MoveList *moves, int from) {
    static const int OFFSETS[8][2] = {
        {-2, -1},
        {-2, 1},
        {-1, -2},
        {-1, 2},
        {1, -2},
        {1, 2},
        {2, -1},
        {2, 1}
    };
    int row = square_row(from);
    int column = square_column(from);
    int index;

    for (index = 0; index < 8; ++index) {
        int to = make_square(
            row + OFFSETS[index][0],
            column + OFFSETS[index][1]
        );
        Piece target;

        if (!is_valid_square(to)) {
            continue;
        }

        target = position_piece_at(position, to);
        if (target == PIECE_NONE) {
            add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_NONE);
        } else if (piece_color(target) != position->side_to_move) {
            add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_CAPTURE);
        }
    }
}

static void generate_sliding_moves(const Position *position, MoveList *moves, int from, const int directions[][2], int direction_count) {
    int start_row = square_row(from);
    int start_column = square_column(from);
    int direction_index;

    for (direction_index = 0;
         direction_index < direction_count;
         ++direction_index) {
        int row = start_row + directions[direction_index][0];
        int column = start_column + directions[direction_index][1];

        while (is_valid_coordinate(row, column)) {
            int to = make_square(row, column);
            Piece target = position_piece_at(position, to);

            if (target == PIECE_NONE) {
                add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_NONE);
            } else {
                if (piece_color(target) != position->side_to_move) {
                    add_move(
                        moves,
                        from,
                        to,
                        PIECE_NONE,
                        MOVE_FLAG_CAPTURE
                    );
                }
                break;
            }

            row += directions[direction_index][0];
            column += directions[direction_index][1];
        }
    }
}

static void generate_castling_moves(const Position *position, MoveList *moves, int from) {
    int row;
    int king_side_right;
    int queen_side_right;
    Piece rook;

    if (position->side_to_move == COLOR_WHITE) {
        row = 7;
        king_side_right = CASTLING_WHITE_KING_SIDE;
        queen_side_right = CASTLING_WHITE_QUEEN_SIDE;
        rook = PIECE_WHITE_ROOK;
    } else {
        row = 0;
        king_side_right = CASTLING_BLACK_KING_SIDE;
        queen_side_right = CASTLING_BLACK_QUEEN_SIDE;
        rook = PIECE_BLACK_ROOK;
    }

    if (from != make_square(row, 4)) {
        return;
    }

    if ((position->castling_rights & king_side_right) &&
        position_piece_at_coordinates(position, row, 5) == PIECE_NONE &&
        position_piece_at_coordinates(position, row, 6) == PIECE_NONE &&
        position_piece_at_coordinates(position, row, 7) == rook) {
        add_move(
            moves,
            from,
            make_square(row, 6),
            PIECE_NONE,
            MOVE_FLAG_CASTLE_KINGSIDE
        );
    }

    if ((position->castling_rights & queen_side_right) &&
        position_piece_at_coordinates(position, row, 1) == PIECE_NONE &&
        position_piece_at_coordinates(position, row, 2) == PIECE_NONE &&
        position_piece_at_coordinates(position, row, 3) == PIECE_NONE &&
        position_piece_at_coordinates(position, row, 0) == rook) {
        add_move(
            moves,
            from,
            make_square(row, 2),
            PIECE_NONE,
            MOVE_FLAG_CASTLE_QUEENSIDE
        );
    }
}

static void generate_king_moves(const Position *position, MoveList *moves, int from) {
    int row = square_row(from);
    int column = square_column(from);
    int row_offset;
    int column_offset;

    for (row_offset = -1; row_offset <= 1; ++row_offset) {
        for (column_offset = -1; column_offset <= 1; ++column_offset) {
            int to;
            Piece target;

            if (row_offset == 0 && column_offset == 0) {
                continue;
            }

            to = make_square(row + row_offset, column + column_offset);
            if (!is_valid_square(to)) {
                continue;
            }

            target = position_piece_at(position, to);
            if (target == PIECE_NONE) {
                add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_NONE);
            } else if (piece_color(target) != position->side_to_move) {
                add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_CAPTURE);
            }
        }
    }

    generate_castling_moves(position, moves, from);
}

void generate_moves(const Position *position, MoveList *moves) {
    static const int BISHOP_DIRECTIONS[4][2] = {
        {-1, -1},
        {-1, 1},
        {1, -1},
        {1, 1}
    };
    static const int ROOK_DIRECTIONS[4][2] = {
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1}
    };
    static const int QUEEN_DIRECTIONS[8][2] = {
        {-1, -1},
        {-1, 1},
        {1, -1},
        {1, 1},
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1}
    };
    int square;

    if (moves == 0) {
        return;
    }

    moves->count = 0;
    if (position == 0) {
        return;
    }

    for (square = 0; square < SQUARE_COUNT; ++square) {
        Piece piece = position_piece_at(position, square);

        if (piece == PIECE_NONE ||
            piece_color(piece) != position->side_to_move) {
            continue;
        }

        switch (piece_type(piece)) {
            case PIECE_TYPE_PAWN:
                generate_pawn_moves(position, moves, square);
                break;
            case PIECE_TYPE_KNIGHT:
                generate_knight_moves(position, moves, square);
                break;
            case PIECE_TYPE_BISHOP:
                generate_sliding_moves(
                    position,
                    moves,
                    square,
                    BISHOP_DIRECTIONS,
                    4
                );
                break;
            case PIECE_TYPE_ROOK:
                generate_sliding_moves(
                    position,
                    moves,
                    square,
                    ROOK_DIRECTIONS,
                    4
                );
                break;
            case PIECE_TYPE_QUEEN:
                generate_sliding_moves(
                    position,
                    moves,
                    square,
                    QUEEN_DIRECTIONS,
                    8
                );
                break;
            case PIECE_TYPE_KING:
                generate_king_moves(position, moves, square);
                break;
            case PIECE_TYPE_NONE:
                break;
        }
    }
}
