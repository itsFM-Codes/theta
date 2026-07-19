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

int find_king(const Position *position, Color color) {
    Piece king;
    int square;

    if (position == 0) {
        return NO_SQUARE;
    }

    if (color == COLOR_WHITE) {
        king = PIECE_WHITE_KING;
    } else if (color == COLOR_BLACK) {
        king = PIECE_BLACK_KING;
    } else {
        return NO_SQUARE;
    }

    for (square = 0; square < SQUARE_COUNT; ++square) {
        if (position_piece_at(position, square) == king) {
            return square;
        }
    }

    return NO_SQUARE;
}

static int is_attacked_by_slider(
    const Position *position,
    int square,
    Color attacking_color,
    const int directions[][2],
    int direction_count,
    PieceType first_type,
    PieceType second_type
) {
    int start_row = square_row(square);
    int start_column = square_column(square);
    int direction_index;

    for (direction_index = 0;
         direction_index < direction_count;
         ++direction_index) {
        int row = start_row + directions[direction_index][0];
        int column = start_column + directions[direction_index][1];

        while (is_valid_coordinate(row, column)) {
            Piece piece = position_piece_at_coordinates(
                position,
                row,
                column
            );

            if (piece != PIECE_NONE) {
                if (piece_color(piece) == attacking_color &&
                    (piece_type(piece) == first_type ||
                     piece_type(piece) == second_type)) {
                    return 1;
                }

                break;
            }

            row += directions[direction_index][0];
            column += directions[direction_index][1];
        }
    }

    return 0;
}

int is_square_attacked(
    const Position *position,
    int square,
    Color attacking_color
) {
    static const int KNIGHT_OFFSETS[8][2] = {
        {-2, -1},
        {-2, 1},
        {-1, -2},
        {-1, 2},
        {1, -2},
        {1, 2},
        {2, -1},
        {2, 1}
    };
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
    int row;
    int column;
    int pawn_row;
    int offset;
    int row_offset;
    int column_offset;
    Piece pawn;
    Piece knight;
    Piece king;

    if (position == 0 ||
        !is_valid_square(square) ||
        attacking_color == COLOR_NONE) {
        return 0;
    }

    row = square_row(square);
    column = square_column(square);

    if (attacking_color == COLOR_WHITE) {
        pawn = PIECE_WHITE_PAWN;
        knight = PIECE_WHITE_KNIGHT;
        king = PIECE_WHITE_KING;
        pawn_row = row + 1;
    } else {
        pawn = PIECE_BLACK_PAWN;
        knight = PIECE_BLACK_KNIGHT;
        king = PIECE_BLACK_KING;
        pawn_row = row - 1;
    }

    // Check pawn attacks
    for (offset = -1; offset <= 1; offset += 2) {
        if (position_piece_at_coordinates(
                position,
                pawn_row,
                column + offset
            ) == pawn) {
            return 1;
        }
    }

    // Check knight attacks
    for (offset = 0; offset < 8; ++offset) {
        if (position_piece_at_coordinates(
                position,
                row + KNIGHT_OFFSETS[offset][0],
                column + KNIGHT_OFFSETS[offset][1]
            ) == knight) {
            return 1;
        }
    }

    // Check diagonal and straight sliding attacks
    if (is_attacked_by_slider(
            position,
            square,
            attacking_color,
            BISHOP_DIRECTIONS,
            4,
            PIECE_TYPE_BISHOP,
            PIECE_TYPE_QUEEN
        )) {
        return 1;
    }

    if (is_attacked_by_slider(
            position,
            square,
            attacking_color,
            ROOK_DIRECTIONS,
            4,
            PIECE_TYPE_ROOK,
            PIECE_TYPE_QUEEN
        )) {
        return 1;
    }

    // Check king attacks
    for (row_offset = -1; row_offset <= 1; ++row_offset) {
        for (column_offset = -1;
             column_offset <= 1;
             ++column_offset) {
            if (row_offset == 0 && column_offset == 0) {
                continue;
            }

            if (position_piece_at_coordinates(
                    position,
                    row + row_offset,
                    column + column_offset
                ) == king) {
                return 1;
            }
        }
    }

    return 0;
}

static int is_castling_path_safe(
    const Position *position,
    Move move,
    Color moving_color
) {
    Position transit_position;
    Color attacking_color = opposite_color(moving_color);
    int row = square_row(move.from);
    int transit_column;
    int transit_square;

    if (is_square_attacked(
            position,
            move.from,
            attacking_color
        )) {
        return 0;
    }

    if (move.flags & MOVE_FLAG_CASTLE_KINGSIDE) {
        transit_column = 5;
    } else {
        transit_column = 3;
    }

    transit_square = make_square(row, transit_column);
    transit_position = *position;
    position_set_piece(&transit_position, move.from, PIECE_NONE);
    position_set_piece(
        &transit_position,
        transit_square,
        position_piece_at(position, move.from)
    );

    return !is_square_attacked(
        &transit_position,
        transit_square,
        attacking_color
    );
}

void generate_legal_moves(Position *position, MoveList *moves) {
    MoveList pseudo_moves;
    Color moving_color;
    int index;

    if (moves == 0) {
        return;
    }

    moves->count = 0;
    if (position == 0) {
        return;
    }

    moving_color = position->side_to_move;
    if (!is_valid_square(find_king(position, moving_color))) {
        return;
    }

    generate_moves(position, &pseudo_moves);

    for (index = 0; index < pseudo_moves.count; ++index) {
        Move move = pseudo_moves.moves[index];
        UndoState undo;
        int king_square;
        int is_legal;

        if (((move.flags & MOVE_FLAG_CASTLE_KINGSIDE) ||
             (move.flags & MOVE_FLAG_CASTLE_QUEENSIDE)) &&
            !is_castling_path_safe(position, move, moving_color)) {
            continue;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        king_square = find_king(position, moving_color);
        is_legal = is_valid_square(king_square) &&
                   !is_square_attacked(
                       position,
                       king_square,
                       position->side_to_move
                   );

        undo_move(position, move, &undo);

        if (is_legal && moves->count < MAX_MOVES) {
            moves->moves[moves->count] = move;
            moves->count++;
        }
    }
}

int make_legal_move(Position *position, Move move, UndoState *undo) {
    Color moving_color;
    int king_square;

    if (position == 0 || undo == 0) {
        return 0;
    }

    moving_color = position->side_to_move;
    if (((move.flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0 ||
         (move.flags & MOVE_FLAG_CASTLE_QUEENSIDE) != 0) &&
        !is_castling_path_safe(position, move, moving_color)) {
        return 0;
    }

    if (!make_move(position, move, undo)) {
        return 0;
    }

    king_square = find_king(position, moving_color);
    if (!is_valid_square(king_square) ||
        is_square_attacked(position, king_square, position->side_to_move)) {
        undo_move(position, move, undo);
        return 0;
    }

    return 1;
}
