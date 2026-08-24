#include "move_ordering.h"

#include "static_exchange.h"

#include "src/chess/movegen.h"
#include "src/eval/evaluation.h"

#define CHECK_MOVE_SCORE 5000
#define KILLER_MOVE_SCORE 4000
#define COUNTERMOVE_SCORE 3500
#define MAX_HISTORY_SCORE 3000
#define TABLE_MOVE_SCORE 20000
#define PROMOTION_MOVE_SCORE 15000
#define GOOD_CAPTURE_SCORE 10000
#define LOSING_CAPTURE_SCORE 1500
#define PAWN_HISTORY_WEIGHT 2
#define CORRECTION_HISTORY_LIMIT 1024

static const int CORRECTION_HISTORY_OFFSETS[2] = {2, 4};

static unsigned int pawn_history_index(const Position *position) {
    uint64_t key;

    if (position == 0) {
        return 0;
    }

    if (position->pawn_history_key_valid &&
        position->pawn_history_side_to_move == position->side_to_move) {
        return (unsigned int)(
            position->pawn_history_key & (PAWN_HISTORY_SIZE - 1)
        );
    }

    key = position->piece_occupied[PIECE_WHITE_PAWN] *
        UINT64_C(0x9e3779b97f4a7c15);
    key ^= position->piece_occupied[PIECE_BLACK_PAWN] *
        UINT64_C(0xbf58476d1ce4e5b9);
    if (position->side_to_move == COLOR_BLACK) {
        key ^= UINT64_C(0x632be59bd9b4e019);
    }
    key ^= key >> 29;
    key *= UINT64_C(0x94d049bb133111eb);
    key ^= key >> 31;
    position->pawn_history_key = key;
    position->pawn_history_key_valid = 1;
    position->pawn_history_side_to_move = position->side_to_move;
    return (unsigned int)(key & (PAWN_HISTORY_SIZE - 1));
}

static void update_history_score(int *score, int bonus) {
    if (score == 0) {
        return;
    }

    *score += bonus - *score * (bonus < 0 ? -bonus : bonus) /
        MAX_HISTORY_SCORE;
    if (*score > MAX_HISTORY_SCORE) {
        *score = MAX_HISTORY_SCORE;
    } else if (*score < -MAX_HISTORY_SCORE) {
        *score = -MAX_HISTORY_SCORE;
    }

}

static void update_continuation_history_score(short *score, int bonus) {
    int updated;

    if (score == 0) {
        return;
    }

    updated = *score;
    updated += bonus - updated * (bonus < 0 ? -bonus : bonus) /
        MAX_HISTORY_SCORE;
    if (updated > MAX_HISTORY_SCORE) {
        updated = MAX_HISTORY_SCORE;
    } else if (updated < -MAX_HISTORY_SCORE) {
        updated = -MAX_HISTORY_SCORE;
    }
    *score = (short)updated;
}

static void update_pawn_history_score(short *score, int bonus) {
    update_continuation_history_score(score, bonus);
}

static int history_bonus(int depth) {
    int bonus = 150 * depth - 100;

    return bonus > 1500 ? 1500 : bonus;
}

static int moves_are_equal(Move first, Move second) {
    return first.from == second.from &&
           first.to == second.to &&
           first.promotion == second.promotion &&
           first.flags == second.flags;
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
        case PIECE_TYPE_KING:
            return KING_VALUE;
        case PIECE_TYPE_NONE:
            return 0;
    }

    return 0;
}

static int move_gives_check(const Position *position, Move move) {
#if !defined(THETA_DISABLE_FAST_CHECK)
    return position_move_gives_check(position, move);
#else
    UndoState undo;
    int gives_check;

    if (!make_move((Position *)position, move, &undo)) {
        return 0;
    }

    gives_check = position_is_in_check(position);
    undo_move((Position *)position, move, &undo);
    return gives_check;
#endif
}

static int killer_move_score(
    const SearchContext *context,
    int ply,
    Move move
) {
    if (context == 0 || ply < 0 || ply >= MAX_KILLER_PLY) {
        return 0;
    }

    if (moves_are_equal(move, context->killer_moves[ply][0])) {
        return KILLER_MOVE_SCORE;
    }

    if (moves_are_equal(move, context->killer_moves[ply][1])) {
        return KILLER_MOVE_SCORE - 1;
    }

    return 0;
}

int quiet_history_score(
    const SearchContext *context,
    const Position *position,
    Color color,
    int ply,
    Move move
) {
    static const int offsets[6] = {1, 2, 4, 6, 8, 12};
    int score;
    int index;
    int offset_count;
    Piece moving_piece;
    PieceType moving_type;

    if (context == 0 || context->shared_state == 0 ||
        context->shared_state->heuristics == 0 ||
        position == 0 || color == COLOR_NONE) {
        return 0;
    }

    score = context->shared_state->heuristics
        ->history[color][move.from][move.to];
    if (ply >= 0 && ply < LOW_PLY_HISTORY_SIZE) {
        score += 8 * context->shared_state->heuristics
            ->low_ply_history[ply][color][move.from][move.to] / (1 + ply);
    }
    moving_piece = position_piece_at(position, move.from);
    if (moving_piece == PIECE_NONE) {
        moving_piece = position_piece_at(position, move.to);
    }
    moving_type = piece_type(moving_piece);
    if (ply >= 2) {
        score += pawn_history_score(
            context,
            position,
            color,
            moving_type,
            move.to
        ) / PAWN_HISTORY_WEIGHT;
    }
    offset_count = ply >= 6 ? 6 : 3;
    for (index = 0; index < offset_count; ++index) {
        int previous_ply = ply - offsets[index];

        if (previous_ply >= 0 && previous_ply < MAX_SEARCH_PLY) {
            Move previous = context->line_moves[previous_ply];
            PieceType previous_type =
                context->line_move_types[previous_ply];

            if (is_valid_square(previous.from) &&
                is_valid_square(previous.to) &&
                previous_type != PIECE_TYPE_NONE &&
                moving_type != PIECE_TYPE_NONE) {
                int continuation = context->shared_state->heuristics
                    ->continuation_history[index][previous_type][previous.to]
                        [moving_type][move.to];

                score += index == 0 ? continuation : continuation / 3;
            }
        }
    }
    return score;
}

int pawn_history_score(
    const SearchContext *context,
    const Position *position,
    Color color,
    PieceType moving_type,
    int target_square
) {
    if (context == 0 || context->shared_state == 0 ||
        context->shared_state->heuristics == 0 || position == 0 ||
        color == COLOR_NONE || moving_type == PIECE_TYPE_NONE ||
        !is_valid_square(target_square)) {
        return 0;
    }

    return context->shared_state->heuristics
        ->pawn_history[color][pawn_history_index(position)]
            [moving_type][target_square];
}

int correction_history_score(
    const SearchContext *context,
    const Position *position,
    int ply
) {
    int score;
    int index;

    if (context == 0 || context->shared_state == 0 ||
        context->shared_state->heuristics == 0 || position == 0) {
        return 0;
    }

    score = context->shared_state->heuristics
        ->correction_history[pawn_history_index(position)];
    for (index = 0; index < 2; ++index) {
        int previous_ply = ply - CORRECTION_HISTORY_OFFSETS[index];
        Move previous_move;
        PieceType previous_type;

        if (previous_ply < 0 || previous_ply >= MAX_SEARCH_PLY) {
            continue;
        }
        previous_move = context->line_moves[previous_ply];
        previous_type = context->line_move_types[previous_ply];
        if (!is_valid_square(previous_move.to) ||
            previous_type == PIECE_TYPE_NONE ||
            previous_type > PIECE_TYPE_KING) {
            continue;
        }
        score += context->shared_state->heuristics
            ->continuation_correction_history[index][previous_type]
                [previous_move.to] * (index == 0 ? 130 : 70) / 128;
    }
    return score;
}

static void update_correction_history_score(short *score, int bonus) {
    int updated;

    if (score == 0) {
        return;
    }

    if (bonus > CORRECTION_HISTORY_LIMIT) {
        bonus = CORRECTION_HISTORY_LIMIT;
    } else if (bonus < -CORRECTION_HISTORY_LIMIT) {
        bonus = -CORRECTION_HISTORY_LIMIT;
    }
    updated = *score + (bonus - *score) / 8;
    if (updated > CORRECTION_HISTORY_LIMIT) {
        updated = CORRECTION_HISTORY_LIMIT;
    } else if (updated < -CORRECTION_HISTORY_LIMIT) {
        updated = -CORRECTION_HISTORY_LIMIT;
    }
    *score = (short)updated;
}

void record_correction_history(
    SearchContext *context,
    const Position *position,
    int ply,
    int score_delta
) {
    int index;

    if (context == 0 || context->shared_state == 0 ||
        context->shared_state->heuristics == 0 || position == 0) {
        return;
    }

    update_correction_history_score(
        &context->shared_state->heuristics
            ->correction_history[pawn_history_index(position)],
        score_delta
    );

    for (index = 0; index < 2; ++index) {
        int previous_ply = ply - CORRECTION_HISTORY_OFFSETS[index];
        Move previous_move;
        PieceType previous_type;
        short *score;
        int bonus;

        if (previous_ply < 0 || previous_ply >= MAX_SEARCH_PLY) {
            continue;
        }
        previous_move = context->line_moves[previous_ply];
        previous_type = context->line_move_types[previous_ply];
        if (!is_valid_square(previous_move.to) ||
            previous_type == PIECE_TYPE_NONE ||
            previous_type > PIECE_TYPE_KING) {
            continue;
        }
        bonus = score_delta * (index == 0 ? 130 : 70) / 128;
        score = &context->shared_state->heuristics
            ->continuation_correction_history[index][previous_type]
                [previous_move.to];
        update_correction_history_score(score, bonus);
    }
}

static int counter_move_score(
    const SearchContext *context,
    int ply,
    Move move
) {
    Move previous_move;
    Move counter_move;

    if (context == 0 || ply <= 0 || ply > MAX_SEARCH_PLY) {
        return 0;
    }

    previous_move = context->line_moves[ply - 1];
    if (!is_valid_square(previous_move.from) ||
        !is_valid_square(previous_move.to)) {
        return 0;
    }

    counter_move = context->counter_moves[previous_move.from][previous_move.to];
    return moves_are_equal(move, counter_move) ? COUNTERMOVE_SCORE : 0;
}

static Piece captured_piece_for_move(const Position *position, Move move) {
    if (move.flags & MOVE_FLAG_EN_PASSANT) {
        return position->side_to_move == COLOR_WHITE
            ? PIECE_BLACK_PAWN
            : PIECE_WHITE_PAWN;
    }

    return position_piece_at(position, move.to);
}

static int move_needs_see(const Position *position, Move move) {
    Piece attacker;
    Piece victim;

    if (position == 0 || (move.flags & MOVE_FLAG_CAPTURE) == 0) {
        return 0;
    }

    attacker = position_piece_at(position, move.from);
    victim = captured_piece_for_move(position, move);
    return piece_value(victim) < piece_value(attacker) ||
           (move.flags & MOVE_FLAG_PROMOTION) != 0;
}

static uint64_t attacks_for_piece_type(
    const Position *position,
    Color color,
    PieceType type
) {
    Piece piece;
    uint64_t pieces;
    uint64_t attacks = 0;

    if (position == 0 || color == COLOR_NONE ||
        type == PIECE_TYPE_NONE || type == PIECE_TYPE_KING) {
        return 0;
    }

    if (type == PIECE_TYPE_PAWN) {
        return position_pawn_attack_map(position, color);
    }

    piece = (Piece)((color == COLOR_WHITE ? PIECE_WHITE_PAWN
                                          : PIECE_BLACK_PAWN) + type -
                    PIECE_TYPE_PAWN);
    pieces = position->piece_occupied[piece];
    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);

        attacks |= position_piece_attack_map(position, square, type);
        pieces &= pieces - 1;
    }
    return attacks;
}

static void initialize_threat_by_lesser(
    const Position *position,
    uint64_t *threat_by_lesser
) {
    Color enemy;
    uint64_t pawn_attacks;
    uint64_t knight_attacks;
    uint64_t bishop_attacks;
    uint64_t rook_attacks;
    int type;

    if (threat_by_lesser == 0) {
        return;
    }
    for (type = 0; type <= PIECE_TYPE_KING; ++type) {
        threat_by_lesser[type] = 0;
    }
    if (position == 0 || position->side_to_move == COLOR_NONE) {
        return;
    }

    enemy = opposite_color(position->side_to_move);
    pawn_attacks = attacks_for_piece_type(
        position, enemy, PIECE_TYPE_PAWN
    );
    knight_attacks = attacks_for_piece_type(
        position, enemy, PIECE_TYPE_KNIGHT
    );
    bishop_attacks = attacks_for_piece_type(
        position, enemy, PIECE_TYPE_BISHOP
    );
    rook_attacks = attacks_for_piece_type(
        position, enemy, PIECE_TYPE_ROOK
    );
    threat_by_lesser[PIECE_TYPE_KNIGHT] = pawn_attacks;
    threat_by_lesser[PIECE_TYPE_BISHOP] = pawn_attacks;
    threat_by_lesser[PIECE_TYPE_ROOK] = pawn_attacks |
        knight_attacks | bishop_attacks;
    threat_by_lesser[PIECE_TYPE_QUEEN] = threat_by_lesser[PIECE_TYPE_ROOK] |
        rook_attacks;
}

int capture_history_score(
    const SearchContext *context,
    Color color,
    PieceType attacker_type,
    int target_square,
    PieceType captured_type
) {
    if (context == 0 || context->shared_state == 0 ||
        context->shared_state->heuristics == 0 || color == COLOR_NONE ||
        attacker_type == PIECE_TYPE_NONE ||
        captured_type == PIECE_TYPE_NONE ||
        !is_valid_square(target_square)) {
        return 0;
    }

    return context->shared_state->heuristics
        ->capture_history[color][attacker_type][target_square][captured_type];
}

static int move_order_score(
    Position *position,
    Move move,
    int order_checks,
    const SearchContext *context,
    int ply,
    const Move *table_move,
    int see_score,
    int see_valid,
    int gives_check,
    int check_valid,
    const uint64_t *threat_by_lesser
) {
    Piece attacker;
    Piece victim;

    if (table_move != 0 && moves_are_equal(move, *table_move)) {
        return TABLE_MOVE_SCORE;
    }

    if ((move.flags & MOVE_FLAG_PROMOTION) != 0 &&
        (move.flags & MOVE_FLAG_CAPTURE) == 0) {
        return PROMOTION_MOVE_SCORE + piece_value(move.promotion);
    }

    if ((move.flags & MOVE_FLAG_CAPTURE) == 0) {
        int killer_score;
        int reply_score;
        int threat_score = 0;

        attacker = position_piece_at(position, move.from);

        if (order_checks && (check_valid
                ? gives_check
                : move_gives_check(position, move))) {
            return CHECK_MOVE_SCORE;
        }

        killer_score = killer_move_score(context, ply, move);
        if (killer_score > 0) {
            return killer_score;
        }

        reply_score = counter_move_score(context, ply, move);
        if (reply_score > 0) {
            return reply_score;
        }

        if (threat_by_lesser != 0) {
            PieceType type = piece_type(attacker);
            if (type >= PIECE_TYPE_PAWN && type <= PIECE_TYPE_QUEEN) {
                threat_score = 20 * (
                    ((threat_by_lesser[type] >> move.from) & 1) -
                    ((threat_by_lesser[type] >> move.to) & 1)
                );
                threat_score *= piece_value(attacker);
            }
        }

        return threat_score + quiet_history_score(
            context, position, position->side_to_move, ply, move
        );
    }

    attacker = position_piece_at(position, move.from);
    victim = captured_piece_for_move(position, move);
    {
        int attacker_value = piece_value(attacker);
        int victim_value = piece_value(victim);
        int promotion_score = (move.flags & MOVE_FLAG_PROMOTION) != 0
            ? (piece_value(move.promotion) - PAWN_VALUE) * 8
            : 0;
        int history_score = capture_history_score(
            context,
            position->side_to_move,
            piece_type(attacker),
            move.to,
            piece_type(victim)
        );
        int value_score = victim_value * 16 - attacker_value;

        if (victim_value < attacker_value ||
            (move.flags & MOVE_FLAG_PROMOTION) != 0) {
            if (!see_valid) {
                see_score = static_exchange_evaluation(position, move);
            }
            if (see_score < 0) {
                return LOSING_CAPTURE_SCORE + history_score + see_score;
            }
        }

        return GOOD_CAPTURE_SCORE + promotion_score + value_score +
            history_score + see_score;
    }
}

static void sort_picker(MovePicker *picker) {
    Move sorted_moves[MAX_MOVES];
    int sorted_scores[MAX_MOVES];
    int sorted_see_scores[MAX_MOVES];
    unsigned char sorted_see_valid[MAX_MOVES];
    unsigned char sorted_gives_check[MAX_MOVES];
    unsigned char sorted_check_valid[MAX_MOVES];
    int index;

    if (picker == 0 || picker->moves == 0) {
        return;
    }

    for (index = 0; index < picker->moves->count; ++index) {
        int insert = index;
        int score = picker->scores[index];
        int see_score = picker->see_scores[index];
        unsigned char see_valid = picker->see_valid[index];
        unsigned char gives_check = picker->gives_check[index];
        unsigned char check_valid = picker->check_valid[index];

        while (insert > 0 && sorted_scores[insert - 1] < score) {
            sorted_scores[insert] = sorted_scores[insert - 1];
            sorted_moves[insert] = sorted_moves[insert - 1];
            sorted_see_scores[insert] = sorted_see_scores[insert - 1];
            sorted_see_valid[insert] = sorted_see_valid[insert - 1];
            sorted_gives_check[insert] = sorted_gives_check[insert - 1];
            sorted_check_valid[insert] = sorted_check_valid[insert - 1];
            insert--;
        }

        sorted_scores[insert] = score;
        sorted_moves[insert] = picker->moves->moves[index];
        sorted_see_scores[insert] = see_score;
        sorted_see_valid[insert] = see_valid;
        sorted_gives_check[insert] = gives_check;
        sorted_check_valid[insert] = check_valid;
    }

    for (index = 0; index < picker->moves->count; ++index) {
        picker->scores[index] = sorted_scores[index];
        picker->moves->moves[index] = sorted_moves[index];
        picker->see_scores[index] = sorted_see_scores[index];
        picker->see_valid[index] = sorted_see_valid[index];
        picker->gives_check[index] = sorted_gives_check[index];
        picker->check_valid[index] = sorted_check_valid[index];
    }
}

void order_moves(
    Position *position,
    MoveList *moves,
    int order_checks,
    const SearchContext *context,
    int ply,
    const Move *table_move
) {
    MovePicker picker;
    Move move;

    initialize_move_picker(
        &picker, position, moves, order_checks, context, ply, table_move
    );
    while (move_picker_next(&picker, &move)) {
    }
}

void initialize_move_picker(
    MovePicker *picker,
    Position *position,
    MoveList *moves,
    int order_checks,
    const SearchContext *context,
    int ply,
    const Move *table_move
) {
    int has_quiet_move = 0;
    int index;

    if (picker == 0) {
        return;
    }

    picker->moves = moves;
    picker->next_index = 0;
    if (position == 0 || moves == 0) {
        return;
    }

    for (index = 0; index < moves->count; ++index) {
        if ((moves->moves[index].flags &
             (MOVE_FLAG_CAPTURE | MOVE_FLAG_PROMOTION)) == 0) {
            has_quiet_move = 1;
            break;
        }
    }
    if (has_quiet_move) {
        initialize_threat_by_lesser(position, picker->threat_by_lesser);
    } else {
        for (index = 0; index <= PIECE_TYPE_KING; ++index) {
            picker->threat_by_lesser[index] = 0;
        }
    }

    for (index = 0; index < moves->count; ++index) {
        picker->gives_check[index] = 0;
        picker->check_valid[index] = 0;
        picker->see_scores[index] = 0;
        picker->see_valid[index] = 0;
        if (order_checks &&
            (moves->moves[index].flags &
             (MOVE_FLAG_CAPTURE | MOVE_FLAG_PROMOTION)) == 0) {
            picker->gives_check[index] = (unsigned char)move_gives_check(
                position,
                moves->moves[index]
            );
            picker->check_valid[index] = 1;
        }
        if (move_needs_see(position, moves->moves[index])) {
            picker->see_scores[index] = static_exchange_evaluation(
                position,
                moves->moves[index]
            );
            picker->see_valid[index] = 1;
        }
        picker->scores[index] = move_order_score(
            position,
            moves->moves[index],
            order_checks,
            context,
            ply,
            table_move,
            picker->see_scores[index],
            picker->see_valid[index],
            picker->gives_check[index],
            picker->check_valid[index],
            picker->threat_by_lesser
        );
    }
    sort_picker(picker);
}

int move_picker_next(MovePicker *picker, Move *move) {
    if (picker == 0 || picker->moves == 0 || move == 0 ||
        picker->next_index >= picker->moves->count) {
        return 0;
    }

    *move = picker->moves->moves[picker->next_index];
    picker->next_index++;
    return 1;
}

void record_quiet_cutoff(
    SearchContext *context,
    const Position *position,
    Color color,
    int ply,
    int depth,
    Move move
) {
    static const int offsets[6] = {1, 2, 4, 6, 8, 12};
    int bonus;
    int *history_score;
    int index;
    int offset_count = ply >= 6 ? 6 : 3;

    if (context == 0 || context->shared_state == 0 ||
        context->shared_state->heuristics == 0 ||
        position == 0 || ply < 0 || ply >= MAX_KILLER_PLY ||
        (move.flags & MOVE_FLAG_CAPTURE) != 0) {
        return;
    }

    if (!moves_are_equal(move, context->killer_moves[ply][0])) {
        context->killer_moves[ply][1] = context->killer_moves[ply][0];
        context->killer_moves[ply][0] = move;
    }

    if (ply > 0 && ply <= MAX_SEARCH_PLY) {
        Move previous_move = context->line_moves[ply - 1];

        if (is_valid_square(previous_move.from) &&
            is_valid_square(previous_move.to)) {
            context->counter_moves[previous_move.from][previous_move.to] = move;
        }
    }

    if (color == COLOR_NONE) {
        return;
    }

    bonus = history_bonus(depth);
    history_score = &context->shared_state->heuristics
        ->history[color][move.from][move.to];
    update_history_score(history_score, bonus);
    if (ply < LOW_PLY_HISTORY_SIZE) {
        update_continuation_history_score(
            &context->shared_state->heuristics
                ->low_ply_history[ply][color][move.from][move.to],
            bonus * 712 / 1024
        );
    }

    update_pawn_history_score(
        &context->shared_state->heuristics
            ->pawn_history[color][pawn_history_index(position)]
                [piece_type(position_piece_at(position, move.from))][move.to],
        bonus
    );

    for (index = 0; index < offset_count; ++index) {
        int previous_ply = ply - offsets[index];

        if (previous_ply >= 0 && previous_ply < MAX_SEARCH_PLY) {
            Move previous_move = context->line_moves[previous_ply];
            PieceType previous_type =
                context->line_move_types[previous_ply];
            PieceType moving_type = piece_type(
                position_piece_at(position, move.from)
            );

            if (is_valid_square(previous_move.from) &&
                is_valid_square(previous_move.to) &&
                previous_type != PIECE_TYPE_NONE &&
                moving_type != PIECE_TYPE_NONE) {
                update_continuation_history_score(
                    &context->shared_state->heuristics
                        ->continuation_history[index][previous_type]
                            [previous_move.to][moving_type][move.to],
                    index == 0 ? bonus : index < 3 ? bonus / 2 : bonus / 4
                );
            }
        }
    }
}

void record_quiet_failures(
    SearchContext *context,
    const Position *position,
    Color color,
    int ply,
    int depth,
    const MoveList *moves,
    int count
) {
    static const int offsets[6] = {1, 2, 4, 6, 8, 12};
    int index;
    int penalty;
    int offset_count = ply >= 6 ? 6 : 3;

    if (context == 0 || context->shared_state == 0 ||
        context->shared_state->heuristics == 0 ||
        position == 0 || color == COLOR_NONE ||
        moves == 0 || depth <= 0) {
        return;
    }

    penalty = -history_bonus(depth);
    for (index = 0; index < count && index < moves->count; ++index) {
        Move move = moves->moves[index];

        if ((move.flags & MOVE_FLAG_CAPTURE) == 0) {
            PieceType moving_type = piece_type(
                position_piece_at(position, move.from)
            );
            int continuation_index;

            update_history_score(
                    &context->shared_state->heuristics
                    ->history[color][move.from][move.to],
                penalty
            );
            if (ply >= 0 && ply < LOW_PLY_HISTORY_SIZE) {
                update_continuation_history_score(
                &context->shared_state->heuristics
                        ->low_ply_history[ply][color][move.from][move.to],
                    penalty * 712 / 1024
                );
            }
            update_pawn_history_score(
                            &context->shared_state->heuristics
                    ->pawn_history[color][pawn_history_index(position)]
                        [moving_type][move.to],
                penalty
            );
            for (continuation_index = 0;
                 continuation_index < offset_count;
                 ++continuation_index) {
                int previous_ply =
                    ply - offsets[continuation_index];

                if (previous_ply >= 0 &&
                    previous_ply < MAX_SEARCH_PLY) {
                    Move previous_move =
                        context->line_moves[previous_ply];
                    PieceType previous_type =
                        context->line_move_types[previous_ply];

                    if (is_valid_square(previous_move.from) &&
                        is_valid_square(previous_move.to) &&
                        previous_type != PIECE_TYPE_NONE &&
                        moving_type != PIECE_TYPE_NONE) {
                        update_continuation_history_score(
                    &context->shared_state->heuristics
                                ->continuation_history[continuation_index]
                                    [previous_type][previous_move.to]
                                        [moving_type][move.to],
                            continuation_index == 0
                                ? penalty
                                : continuation_index < 3
                                    ? penalty / 2
                                    : penalty / 4
                        );
                    }
                }
            }
        }
    }
}

void record_capture_cutoff(
    SearchContext *context,
    const Position *position,
    Color color,
    int depth,
    Move move
) {
    Piece attacker;
    Piece victim;
    int *history_score;

    if (context == 0 || context->shared_state == 0 ||
        context->shared_state->heuristics == 0 ||
        position == 0 || color == COLOR_NONE ||
        depth <= 0 || (move.flags & MOVE_FLAG_CAPTURE) == 0) {
        return;
    }

    attacker = position_piece_at(position, move.from);
    victim = captured_piece_for_move(position, move);
    if (piece_type(attacker) == PIECE_TYPE_NONE ||
        piece_type(victim) == PIECE_TYPE_NONE) {
        return;
    }

    history_score = &context->shared_state->heuristics
        ->capture_history[color][piece_type(attacker)]
            [move.to][piece_type(victim)];
    update_history_score(history_score, history_bonus(depth));
}

void record_capture_failures(
    SearchContext *context,
    const Position *position,
    Color color,
    int depth,
    const MoveList *moves,
    int count
) {
    int index;
    int penalty;

    if (context == 0 || context->shared_state == 0 ||
        context->shared_state->heuristics == 0 ||
        position == 0 || color == COLOR_NONE ||
        moves == 0 || depth <= 0) {
        return;
    }

    penalty = -history_bonus(depth);
    for (index = 0; index < count && index < moves->count; ++index) {
        Move move = moves->moves[index];
        Piece attacker;
        Piece victim;

        if ((move.flags & MOVE_FLAG_CAPTURE) == 0) {
            continue;
        }

        attacker = position_piece_at(position, move.from);
        victim = captured_piece_for_move(position, move);
        if (piece_type(attacker) == PIECE_TYPE_NONE ||
            piece_type(victim) == PIECE_TYPE_NONE) {
            continue;
        }

        update_history_score(
        &context->shared_state->heuristics
                ->capture_history[color][piece_type(attacker)]
                    [move.to][piece_type(victim)],
            penalty
        );
    }
}
