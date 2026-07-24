#include "movegen.h"

static uint64_t knight_attack_masks[SQUARE_COUNT];
static uint64_t king_attack_masks[SQUARE_COUNT];
static uint64_t pawn_attackers[COLOR_NONE][SQUARE_COUNT];
static int attack_masks_initialized;

static int pop_first_square(uint64_t *squares) {
    uint64_t value = *squares;
    int square;

    if (value == 0) {
        return NO_SQUARE;
    }

    square = __builtin_ctzll(value);
    *squares = value & (value - 1);
    return square;
}

static void add_attack(uint64_t *mask, int row, int column) {
    int square = make_square(row, column);

    if (is_valid_square(square)) {
        *mask |= UINT64_C(1) << square;
    }
}

static void initialize_attack_masks(void) {
    static const int KNIGHT_OFFSETS[8][2] = {
        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
        {1, -2}, {1, 2}, {2, -1}, {2, 1}
    };
    int square;

    if (attack_masks_initialized) {
        return;
    }

    for (square = 0; square < SQUARE_COUNT; ++square) {
        int row = square_row(square);
        int column = square_column(square);
        int index;
        int row_offset;
        int column_offset;

        for (index = 0; index < 8; ++index) {
            add_attack(
                &knight_attack_masks[square],
                row + KNIGHT_OFFSETS[index][0],
                column + KNIGHT_OFFSETS[index][1]
            );
        }

        for (row_offset = -1; row_offset <= 1; ++row_offset) {
            for (column_offset = -1; column_offset <= 1; ++column_offset) {
                if (row_offset == 0 && column_offset == 0) {
                    continue;
                }
                add_attack(
                    &king_attack_masks[square],
                    row + row_offset,
                    column + column_offset
                );
            }
        }

        add_attack(&pawn_attackers[COLOR_WHITE][square], row + 1, column - 1);
        add_attack(&pawn_attackers[COLOR_WHITE][square], row + 1, column + 1);
        add_attack(&pawn_attackers[COLOR_BLACK][square], row - 1, column - 1);
        add_attack(&pawn_attackers[COLOR_BLACK][square], row - 1, column + 1);
    }

    attack_masks_initialized = 1;
}

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
    Piece piece = position != 0 && is_valid_square(square)
        ? position->board[square]
        : PIECE_NONE;

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

static void generate_pawn_moves(
    const Position *position,
    MoveList *moves,
    int from,
    int tactical_only
) {
    const Piece *board = position->board;
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

    if (is_valid_square(to) && board[to] == PIECE_NONE) {
        if (next_row == promotion_row) {
            add_promotion_moves(
                position,
                moves,
                from,
                to,
                MOVE_FLAG_PROMOTION
            );
        } else if (!tactical_only) {
            add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_NONE);

            if (row == starting_row) {
                int double_to = make_square(row + direction * 2, column);

                if (is_valid_square(double_to) && board[double_to] == PIECE_NONE) {
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

static void generate_knight_moves(
    const Position *position,
    MoveList *moves,
    int from,
    int tactical_only
) {
    const Piece *board = position->board;
    uint64_t targets;

    initialize_attack_masks();
    targets = knight_attack_masks[from] &
        ~position->color_occupied[position->side_to_move];
    if (tactical_only) {
        targets &= position->color_occupied[
            opposite_color(position->side_to_move)
        ];
    }

    while (targets != 0) {
        int to = pop_first_square(&targets);
        Piece target;

        target = board[to];
        if (target == PIECE_NONE && !tactical_only) {
            add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_NONE);
        } else if (target != PIECE_NONE) {
            add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_CAPTURE);
        }
    }
}

static void generate_sliding_moves(
    const Position *position,
    MoveList *moves,
    int from,
    const int directions[][2],
    int direction_count,
    int tactical_only
) {
    int start_row = square_row(from);
    int start_column = square_column(from);
    int direction_index;
    const Piece *board = position->board;

    for (direction_index = 0;
         direction_index < direction_count;
         ++direction_index) {
        int row = start_row + directions[direction_index][0];
        int column = start_column + directions[direction_index][1];

        while (is_valid_coordinate(row, column)) {
            int to = make_square(row, column);
            Piece target = board[to];

            if (target == PIECE_NONE) {
                if (tactical_only) {
                    row += directions[direction_index][0];
                    column += directions[direction_index][1];
                    continue;
                }
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
    const Piece *board = position->board;

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
        board[make_square(row, 5)] == PIECE_NONE &&
        board[make_square(row, 6)] == PIECE_NONE &&
        board[make_square(row, 7)] == rook) {
        add_move(
            moves,
            from,
            make_square(row, 6),
            PIECE_NONE,
            MOVE_FLAG_CASTLE_KINGSIDE
        );
    }

    if ((position->castling_rights & queen_side_right) &&
        board[make_square(row, 1)] == PIECE_NONE &&
        board[make_square(row, 2)] == PIECE_NONE &&
        board[make_square(row, 3)] == PIECE_NONE &&
        board[make_square(row, 0)] == rook) {
        add_move(
            moves,
            from,
            make_square(row, 2),
            PIECE_NONE,
            MOVE_FLAG_CASTLE_QUEENSIDE
        );
    }
}

static void generate_king_moves(
    const Position *position,
    MoveList *moves,
    int from,
    int tactical_only
) {
    const Piece *board = position->board;
    uint64_t targets;

    initialize_attack_masks();
    targets = king_attack_masks[from] &
        ~position->color_occupied[position->side_to_move];
    if (tactical_only) {
        targets &= position->color_occupied[
            opposite_color(position->side_to_move)
        ];
    }

    while (targets != 0) {
        int to = pop_first_square(&targets);
        Piece target = board[to];

        if (target == PIECE_NONE && !tactical_only) {
            add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_NONE);
        } else if (target != PIECE_NONE) {
            add_move(moves, from, to, PIECE_NONE, MOVE_FLAG_CAPTURE);
        }
    }

    if (!tactical_only) {
        generate_castling_moves(position, moves, from);
    }
}

static void generate_moves_internal(
    const Position *position,
    MoveList *moves,
    int tactical_only
) {
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
    uint64_t pieces;
    const Piece *board = position->board;

    if (moves == 0) {
        return;
    }

    moves->count = 0;
    if (position == 0) {
        return;
    }

    if (position->side_to_move == COLOR_NONE) {
        return;
    }

    pieces = position->color_occupied[position->side_to_move];
    while (pieces != 0) {
        int square = pop_first_square(&pieces);
        Piece piece = board[square];

        if (piece == PIECE_NONE ||
            piece_color(piece) != position->side_to_move) {
            continue;
        }

        switch (piece_type(piece)) {
            case PIECE_TYPE_PAWN:
                generate_pawn_moves(position, moves, square, tactical_only);
                break;
            case PIECE_TYPE_KNIGHT:
                generate_knight_moves(position, moves, square, tactical_only);
                break;
            case PIECE_TYPE_BISHOP:
                generate_sliding_moves(
                    position,
                    moves,
                    square,
                    BISHOP_DIRECTIONS,
                    4,
                    tactical_only
                );
                break;
            case PIECE_TYPE_ROOK:
                generate_sliding_moves(
                    position,
                    moves,
                    square,
                    ROOK_DIRECTIONS,
                    4,
                    tactical_only
                );
                break;
            case PIECE_TYPE_QUEEN:
                generate_sliding_moves(
                    position,
                    moves,
                    square,
                    QUEEN_DIRECTIONS,
                    8,
                    tactical_only
                );
                break;
            case PIECE_TYPE_KING:
                generate_king_moves(position, moves, square, tactical_only);
                break;
            case PIECE_TYPE_NONE:
                break;
        }
    }
}

void generate_moves(const Position *position, MoveList *moves) {
    generate_moves_internal(position, moves, 0);
}

void generate_tactical_moves(const Position *position, MoveList *moves) {
    generate_moves_internal(position, moves, 1);
}

int find_king(const Position *position, Color color) {
    if (position == 0) {
        return NO_SQUARE;
    }

    if (color == COLOR_WHITE) {
        return position->white_king_square;
    } else if (color == COLOR_BLACK) {
        return position->black_king_square;
    } else {
        return NO_SQUARE;
    }
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
    Piece pawn;
    Piece knight;
    Piece king;

    if (position == 0 ||
        !is_valid_square(square) ||
        attacking_color == COLOR_NONE) {
        return 0;
    }

    initialize_attack_masks();

    if (attacking_color == COLOR_WHITE) {
        pawn = PIECE_WHITE_PAWN;
        knight = PIECE_WHITE_KNIGHT;
        king = PIECE_WHITE_KING;
    } else {
        pawn = PIECE_BLACK_PAWN;
        knight = PIECE_BLACK_KNIGHT;
        king = PIECE_BLACK_KING;
    }

    if ((pawn_attackers[attacking_color][square] &
         position->piece_occupied[pawn]) != 0) {
        return 1;
    }

    if ((knight_attack_masks[square] & position->piece_occupied[knight]) != 0) {
        return 1;
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

    if ((king_attack_masks[square] & position->piece_occupied[king]) != 0) {
        return 1;
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
