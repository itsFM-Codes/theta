#include "movegen.h"

static uint64_t knight_attack_masks[SQUARE_COUNT];
static uint64_t king_attack_masks[SQUARE_COUNT];
static uint64_t pawn_attackers[COLOR_NONE][SQUARE_COUNT];
static uint64_t slider_rays[SQUARE_COUNT][8];
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

static void build_attack_masks(void) {
    static const int KNIGHT_OFFSETS[8][2] = {
        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
        {1, -2}, {1, 2}, {2, -1}, {2, 1}
    };
    int square;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        int row = square_row(square);
        int column = square_column(square);
        int index;
        int row_offset;
        int column_offset;

        static const int SLIDER_DIRECTIONS[8][2] = {
            {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
            {-1, 0}, {1, 0}, {0, -1}, {0, 1}
        };

        for (index = 0; index < 8; ++index) {
            int ray_row = row + SLIDER_DIRECTIONS[index][0];
            int ray_column = column + SLIDER_DIRECTIONS[index][1];

            while (is_valid_coordinate(ray_row, ray_column)) {
                slider_rays[square][index] |= UINT64_C(1) << make_square(
                    ray_row,
                    ray_column
                );
                ray_row += SLIDER_DIRECTIONS[index][0];
                ray_column += SLIDER_DIRECTIONS[index][1];
            }
        }

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

static inline void initialize_attack_masks(void) {
    if (!attack_masks_initialized) {
        build_attack_masks();
    }
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
    PieceType type,
    int tactical_only
) {
    uint64_t targets = position_piece_attack_map(position, from, type);
    const Piece *board = position->board;

    targets &= tactical_only
        ? position->color_occupied[opposite_color(position->side_to_move)]
        : ~position->color_occupied[position->side_to_move];
    while (targets != 0) {
        int to = pop_first_square(&targets);
        Piece target = board[to];

        add_move(
            moves,
            from,
            to,
            PIECE_NONE,
            target == PIECE_NONE ? MOVE_FLAG_NONE : MOVE_FLAG_CAPTURE
        );
    }
}

uint64_t position_piece_attack_map(
    const Position *position,
    int square,
    PieceType type
) {
    static const int SLIDER_DIRECTIONS[8][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    int first_direction;
    int last_direction;
    int cache_index = -1;
    uint64_t attacks = 0;
    int direction;

    if (position == 0 || !is_valid_square(square)) {
        return 0;
    }

    initialize_attack_masks();
    if (type == PIECE_TYPE_BISHOP) {
        first_direction = 0;
        last_direction = 3;
        cache_index = 0;
    } else if (type == PIECE_TYPE_ROOK) {
        first_direction = 4;
        last_direction = 7;
        cache_index = 1;
    } else if (type == PIECE_TYPE_QUEEN) {
        first_direction = 0;
        last_direction = 7;
        cache_index = 2;
    } else {
        return type == PIECE_TYPE_KNIGHT
            ? knight_attack_masks[square]
            : (type == PIECE_TYPE_KING ? king_attack_masks[square] : 0);
    }

    if (position->sliding_attack_cache_valid[square][cache_index] &&
        position->sliding_attack_cache_keys[square][cache_index] ==
            position->occupied) {
        return position->sliding_attack_cache[square][cache_index];
    }

    for (direction = first_direction;
         direction <= last_direction;
         ++direction) {
        uint64_t ray = slider_rays[square][direction];
        uint64_t blockers = ray & position->occupied;

        if (blockers == 0) {
            attacks |= ray;
        } else {
            int blocker = (SLIDER_DIRECTIONS[direction][0] > 0 ||
                           (SLIDER_DIRECTIONS[direction][0] == 0 &&
                            SLIDER_DIRECTIONS[direction][1] > 0))
                ? __builtin_ctzll(blockers)
                : 63 - __builtin_clzll(blockers);

            attacks |= ray & ~slider_rays[blocker][direction];
        }
    }

    position->sliding_attack_cache[square][cache_index] = attacks;
    position->sliding_attack_cache_keys[square][cache_index] =
        position->occupied;
    position->sliding_attack_cache_valid[square][cache_index] = 1;
    return attacks;
}

static uint64_t piece_attack_map_with_occupancy(
    int square,
    PieceType type,
    uint64_t occupied
) {
    static const int SLIDER_DIRECTIONS[8][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    int first_direction;
    int last_direction;
    uint64_t attacks = 0;
    int direction;

    if (!is_valid_square(square)) {
        return 0;
    }
    initialize_attack_masks();
    if (type == PIECE_TYPE_KNIGHT) {
        return knight_attack_masks[square];
    }
    if (type == PIECE_TYPE_KING) {
        return king_attack_masks[square];
    }
    if (type == PIECE_TYPE_BISHOP) {
        first_direction = 0;
        last_direction = 3;
    } else if (type == PIECE_TYPE_ROOK) {
        first_direction = 4;
        last_direction = 7;
    } else if (type == PIECE_TYPE_QUEEN) {
        first_direction = 0;
        last_direction = 7;
    } else {
        return 0;
    }

    for (direction = first_direction;
         direction <= last_direction;
         ++direction) {
        uint64_t ray = slider_rays[square][direction];
        uint64_t blockers = ray & occupied;

        if (blockers == 0) {
            attacks |= ray;
        } else {
            int blocker = (SLIDER_DIRECTIONS[direction][0] > 0 ||
                           (SLIDER_DIRECTIONS[direction][0] == 0 &&
                            SLIDER_DIRECTIONS[direction][1] > 0))
                ? __builtin_ctzll(blockers)
                : 63 - __builtin_clzll(blockers);

            attacks |= ray & ~slider_rays[blocker][direction];
        }
    }
    return attacks;
}

int position_move_gives_check(const Position *position, Move move) {
    uint64_t pieces[PIECE_BLACK_KING + 1];
    uint64_t occupied;
    uint64_t from_mask;
    uint64_t to_mask;
    uint64_t captured_mask;
    Piece moved_piece;
    Piece placed_piece;
    Piece captured_piece;
    Color moving_color;
    int enemy_king;
    int captured_square;
    int type;

    if (position == 0 || !is_valid_square(move.from) ||
        !is_valid_square(move.to) || move.from == move.to) {
        return 0;
    }

    initialize_attack_masks();

    moved_piece = position_piece_at(position, move.from);
    moving_color = position->side_to_move;
    if (moved_piece == PIECE_NONE ||
        piece_color(moved_piece) != moving_color) {
        return 0;
    }

    enemy_king = find_king(position, opposite_color(moving_color));
    if (!is_valid_square(enemy_king)) {
        return 0;
    }

    for (type = 0; type <= PIECE_BLACK_KING; ++type) {
        pieces[type] = position->piece_occupied[type];
    }

    from_mask = UINT64_C(1) << move.from;
    to_mask = UINT64_C(1) << move.to;
    captured_square = move.to;
    captured_piece = position_piece_at(position, move.to);
    if ((move.flags & MOVE_FLAG_EN_PASSANT) != 0) {
        captured_square = moving_color == COLOR_WHITE
            ? move.to + BOARD_SIZE
            : move.to - BOARD_SIZE;
        captured_piece = position_piece_at(position, captured_square);
    }
    /* Pseudo-move lists can contain a king capture.  The legacy make/undo
     * check helper treats that malformed child as having no king and returns
     * false; keep the fast helper identical for move-ordering callers. */
    if (piece_type(captured_piece) == PIECE_TYPE_KING) {
        return 0;
    }

    placed_piece = (move.flags & MOVE_FLAG_PROMOTION) != 0
        ? move.promotion
        : moved_piece;
    pieces[moved_piece] &= ~from_mask;
    if (captured_piece != PIECE_NONE &&
        is_valid_square(captured_square)) {
        captured_mask = UINT64_C(1) << captured_square;
        pieces[captured_piece] &= ~captured_mask;
    }
    pieces[placed_piece] |= to_mask;

    occupied = position->occupied & ~from_mask;
    if (captured_piece != PIECE_NONE &&
        is_valid_square(captured_square)) {
        occupied &= ~(UINT64_C(1) << captured_square);
    }
    occupied |= to_mask;

    if ((move.flags & (MOVE_FLAG_CASTLE_KINGSIDE |
                       MOVE_FLAG_CASTLE_QUEENSIDE)) != 0) {
        int row = square_row(move.from);
        int kingside = (move.flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0;
        int rook_from = make_square(row, kingside ? 7 : 0);
        int rook_to = make_square(row, kingside ? 5 : 3);
        Piece rook = position_piece_at(position, rook_from);

        if (rook != PIECE_NONE && is_valid_square(rook_from) &&
            is_valid_square(rook_to)) {
            pieces[rook] &= ~(UINT64_C(1) << rook_from);
            pieces[rook] |= UINT64_C(1) << rook_to;
            occupied &= ~(UINT64_C(1) << rook_from);
            occupied |= UINT64_C(1) << rook_to;
        }
    }

    if ((pawn_attackers[moving_color][enemy_king] &
         pieces[moving_color == COLOR_WHITE
             ? PIECE_WHITE_PAWN
             : PIECE_BLACK_PAWN]) != 0) {
        return 1;
    }
    if ((knight_attack_masks[enemy_king] &
         pieces[moving_color == COLOR_WHITE
             ? PIECE_WHITE_KNIGHT
             : PIECE_BLACK_KNIGHT]) != 0) {
        return 1;
    }
    if ((piece_attack_map_with_occupancy(
            enemy_king,
            PIECE_TYPE_BISHOP,
            occupied
        ) & (pieces[moving_color == COLOR_WHITE
                ? PIECE_WHITE_BISHOP
                : PIECE_BLACK_BISHOP] |
            pieces[moving_color == COLOR_WHITE
                ? PIECE_WHITE_QUEEN
                : PIECE_BLACK_QUEEN])) != 0) {
        return 1;
    }
    if ((piece_attack_map_with_occupancy(
            enemy_king,
            PIECE_TYPE_ROOK,
            occupied
        ) & (pieces[moving_color == COLOR_WHITE
                ? PIECE_WHITE_ROOK
                : PIECE_BLACK_ROOK] |
            pieces[moving_color == COLOR_WHITE
                ? PIECE_WHITE_QUEEN
                : PIECE_BLACK_QUEEN])) != 0) {
        return 1;
    }
    return (king_attack_masks[enemy_king] &
            pieces[moving_color == COLOR_WHITE
                ? PIECE_WHITE_KING
                : PIECE_BLACK_KING]) != 0;
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
                    PIECE_TYPE_BISHOP,
                    tactical_only
                );
                break;
            case PIECE_TYPE_ROOK:
                generate_sliding_moves(
                    position,
                    moves,
                    square,
                    PIECE_TYPE_ROOK,
                    tactical_only
                );
                break;
            case PIECE_TYPE_QUEEN:
                generate_sliding_moves(
                    position,
                    moves,
                    square,
                    PIECE_TYPE_QUEEN,
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

uint64_t position_pawn_attack_map(const Position *position, Color color) {
    const uint64_t file_a = UINT64_C(0x0101010101010101);
    const uint64_t file_h = UINT64_C(0x8080808080808080);
    Piece pawn;
    uint64_t pawns;

    if (position == 0 || color == COLOR_NONE) {
        return 0;
    }

    pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    pawns = position->piece_occupied[pawn];
    if (color == COLOR_WHITE) {
        return ((pawns & ~file_a) >> 9) | ((pawns & ~file_h) >> 7);
    }
    return ((pawns & ~file_a) << 7) | ((pawns & ~file_h) << 9);
}

static uint64_t build_position_attack_map(
    const Position *position,
    Color color
) {
    uint64_t attacks;
    uint64_t pieces;

    if (position == 0 || color == COLOR_NONE) {
        return 0;
    }

    initialize_attack_masks();
    attacks = position_pawn_attack_map(position, color);
    pieces = position->color_occupied[color];
    while (pieces != 0) {
        int square = pop_first_square(&pieces);
        PieceType type = piece_type(position_piece_at(position, square));

        if (type == PIECE_TYPE_KNIGHT) {
            attacks |= knight_attack_masks[square];
        } else if (type == PIECE_TYPE_KING) {
            attacks |= king_attack_masks[square];
        } else if (type == PIECE_TYPE_BISHOP ||
                   type == PIECE_TYPE_ROOK ||
                   type == PIECE_TYPE_QUEEN) {
            attacks |= position_piece_attack_map(position, square, type);
        }
    }

    return attacks;
}

uint64_t position_attack_map(const Position *position, Color color) {
    if (position == 0 || color == COLOR_NONE) {
        return 0;
    }

    if ((position->attack_map_cache_valid & (1 << color)) == 0) {
        position->attack_map_cache[color] = build_position_attack_map(
            position,
            color
        );
        position->attack_map_cache_valid |= 1 << color;
    }
    return position->attack_map_cache[color];
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
    Piece slider = attacking_color == COLOR_WHITE
        ? (first_type == PIECE_TYPE_BISHOP
            ? PIECE_WHITE_BISHOP : PIECE_WHITE_ROOK)
        : (first_type == PIECE_TYPE_BISHOP
            ? PIECE_BLACK_BISHOP : PIECE_BLACK_ROOK);
    Piece queen = attacking_color == COLOR_WHITE
        ? PIECE_WHITE_QUEEN
        : PIECE_BLACK_QUEEN;
    PieceType slider_type = first_type == PIECE_TYPE_BISHOP
        ? PIECE_TYPE_BISHOP
        : PIECE_TYPE_ROOK;

    (void)directions;
    (void)direction_count;
    (void)second_type;
    return (position_piece_attack_map(position, square, slider_type) &
            (position->piece_occupied[slider] |
             position->piece_occupied[queen])) != 0;
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

/*
 * Return whether the piece on 'from' is the only blocker between its king
 * and an enemy rook, bishop, or queen.  This is deliberately conservative:
 * callers use it only to skip a post-move king attack test, so every case
 * which is not an obvious line pin returns false.
 */
uint64_t position_pinned_pieces(
    const Position *position,
    Color color
) {
    static const int DIRECTIONS[8][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    int king_square;
    uint64_t pinned = 0;
    int direction;

    if (position == 0 || color == COLOR_NONE) {
        return 0;
    }

    king_square = find_king(position, color);
    if (!is_valid_square(king_square)) {
        return 0;
    }

    for (direction = 0; direction < 8; ++direction) {
        int row = square_row(king_square) + DIRECTIONS[direction][0];
        int column = square_column(king_square) + DIRECTIONS[direction][1];
        int candidate = NO_SQUARE;

        while (is_valid_coordinate(row, column)) {
            int square = make_square(row, column);
            Piece piece = position_piece_at(position, square);

            if (piece == PIECE_NONE) {
                row += DIRECTIONS[direction][0];
                column += DIRECTIONS[direction][1];
                continue;
            }

            if (candidate == NO_SQUARE) {
                if (piece_color(piece) == color) {
                    candidate = square;
                    row += DIRECTIONS[direction][0];
                    column += DIRECTIONS[direction][1];
                    continue;
                }
                break;
            }

            {
                PieceType type = piece_type(piece);
                int diagonal = DIRECTIONS[direction][0] != 0 &&
                    DIRECTIONS[direction][1] != 0;
                int orthogonal = !diagonal;
                int slider = (diagonal &&
                              (type == PIECE_TYPE_BISHOP ||
                               type == PIECE_TYPE_QUEEN)) ||
                             (orthogonal &&
                              (type == PIECE_TYPE_ROOK ||
                               type == PIECE_TYPE_QUEEN));

                if (piece_color(piece) == opposite_color(color) && slider) {
                    pinned |= UINT64_C(1) << candidate;
                }
            }
            break;
        }
    }

    return pinned;
}

int make_legal_move_with_context(
    Position *position,
    Move move,
    UndoState *undo,
    int in_check,
    uint64_t pinned_pieces
) {
    Piece moved_piece;
    Color moving_color;
    int king_square;
    int skip_post_move_check = 0;

    if (position == 0 || undo == 0) {
        return 0;
    }

    moving_color = position->side_to_move;
    moved_piece = position_piece_at(position, move.from);
    king_square = find_king(position, moving_color);
    if (in_check < 0) {
        in_check = is_valid_square(king_square) &&
            is_square_attacked(
                position,
                king_square,
                opposite_color(moving_color)
            );
    }

    /* A non-king move from an unpinned square cannot expose the king to a
     * discovered slider attack. En passant is the exception because it
     * removes a second square, and check/castling still require full tests. */
    if (!in_check && is_valid_square(king_square) &&
        piece_type(moved_piece) != PIECE_TYPE_KING &&
        (pinned_pieces & (UINT64_C(1) << move.from)) == 0 &&
        (move.flags & (MOVE_FLAG_EN_PASSANT |
                       MOVE_FLAG_CASTLE_KINGSIDE |
                       MOVE_FLAG_CASTLE_QUEENSIDE)) == 0) {
        skip_post_move_check = 1;
    }

    if ((move.flags & (MOVE_FLAG_CASTLE_KINGSIDE |
                       MOVE_FLAG_CASTLE_QUEENSIDE)) != 0 &&
        !is_castling_path_safe(position, move, moving_color)) {
        return 0;
    }

    if (!make_move(position, move, undo)) {
        return 0;
    }

    if (skip_post_move_check) {
        return 1;
    }

    {
        int king_square = find_king(position, moving_color);

        if (!is_valid_square(king_square) ||
            is_square_attacked(position, king_square, position->side_to_move)) {
            undo_move(position, move, undo);
            return 0;
        }
    }

    return 1;
}

int make_legal_move_with_check(
    Position *position,
    Move move,
    UndoState *undo,
    int in_check
) {
    return make_legal_move_with_context(
        position,
        move,
        undo,
        in_check,
        position_pinned_pieces(
            position,
            position == 0 ? COLOR_NONE : position->side_to_move
        )
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
