#include "search_internal.h"

#include "move_ordering.h"
#include "quiescence.h"
#include "static_exchange.h"

#include "src/chess/movegen.h"
#include "src/chess/zobrist.h"
#include "src/config/config.h"
#include "src/eval/evaluation.h"
#include "src/eval/nnue.h"

#define REVERSE_FUTILITY_MARGIN 120
#define LATE_MOVE_PRUNING_START 3
#define STATIC_FUTILITY_MARGIN 105
#define RAZORING_MARGIN 180
#define SEARCH_HISTORY_LIMIT 3000
#define MAX_CHECK_EXTENSION_PLY 16
#define MAX_LMR_DEPTH 64
#define SMALL_PROBCUT_MARGIN 280
#define HISTORY_PRUNE_BASE 1400

/*
 * Cut nodes are expected to fail high, so their late moves can be searched
 * more selectively than moves on an all/PV node.  This is the small, stable
 * part of Stockfish's cut-node LMR scheme; the recursive flag is kept
 * explicit so it can be measured independently from evaluation changes.
 */
#define THETA_CUT_NODE_LMR_BONUS 1

static int lmr_reductions[MAX_LMR_DEPTH][MAX_MOVES];
static int lmr_reductions_initialized = 0;
static int lmr_depth_start_cached = -1;
static int lmr_move_start_cached = -1;

static int nnue_profile_value(
    int configured_value,
    int classical_default,
    int nnue_default
) {
    if (nnue_is_enabled() && configured_value == classical_default) {
        return nnue_default;
    }
    return configured_value;
}

static int effective_lmr_depth_start(void) {
    return nnue_profile_value(
        g_config.search_lmr_depth_start,
        4,
        2
    );
}

static int effective_lmr_move_start(void) {
    return nnue_profile_value(
        g_config.search_lmr_move_start,
        4,
        2
    );
}

static int effective_null_move_base(void) {
    return nnue_profile_value(
        g_config.search_null_move_base,
        3,
        3
    );
}

static int effective_static_futility_margin(void) {
    return nnue_profile_value(
        g_config.search_static_futility_margin,
        96,
        95
    );
}

static void initialize_lmr_reductions(void) {
    int depth_start = effective_lmr_depth_start();
    int move_start = effective_lmr_move_start();
    int depth;
    int move_index;

    if (lmr_reductions_initialized &&
        lmr_depth_start_cached == depth_start &&
        lmr_move_start_cached == move_start) {
        return;
    }

    for (depth = 0; depth < MAX_LMR_DEPTH; ++depth) {
        for (move_index = 0; move_index < MAX_MOVES; ++move_index) {
            int reduction = 0;

            if (depth >= depth_start && move_index >= move_start) {
                reduction = 1;
                if (depth >= 5 && move_index >= 6) {
                    reduction++;
                }
                if (depth >= 8 && move_index >= 12) {
                    reduction++;
                }
                if (depth >= 12 && move_index >= 24) {
                    reduction++;
                }
                if (reduction > depth - 2) {
                    reduction = depth - 2;
                }
            }
            lmr_reductions[depth][move_index] = reduction;
        }
    }

    lmr_reductions_initialized = 1;
    lmr_depth_start_cached = depth_start;
    lmr_move_start_cached = move_start;
}

static int late_move_reduction(int depth, int move_index) {
    initialize_lmr_reductions();

    if (depth >= MAX_LMR_DEPTH) {
        depth = MAX_LMR_DEPTH - 1;
    }
    if (move_index >= MAX_MOVES) {
        move_index = MAX_MOVES - 1;
    }
    return depth < 0 || move_index < 0 ? 0 : lmr_reductions[depth][move_index];
}

int search_score_to_table(int score, int ply) {
    if (score > SEARCH_CHECKMATE - MAX_PRINCIPAL_VARIATION) {
        return score + ply;
    }

    if (score < -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION) {
        return score - ply;
    }

    return score;
}

int search_score_from_table(int score, int ply) {
    if (score > SEARCH_CHECKMATE - MAX_PRINCIPAL_VARIATION) {
        return score - ply;
    }

    if (score < -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION) {
        return score + ply;
    }

    return score;
}

static int probe_search_transposition_table_mode(
    SearchContext *context,
    uint64_t key,
    int depth,
    int alpha,
    int beta,
    int ply,
    int *score,
    Move *best_move,
    int exact_only,
    TranspositionEntry *result
) {
    TranspositionEntry entry;
    int table_score;

    if (context == 0 || context->shared_state == 0) {
        return 0;
    }

    context->transposition_statistics.probes++;
    if (!probe_transposition_entry(
            &context->shared_state->transposition_table,
            key,
            &entry
        )) {
        return 0;
    }

    context->transposition_statistics.key_hits++;
    if (best_move != 0) {
        *best_move = entry.best_move;
    }
    if (result != 0) {
        *result = entry;
    }
    if (entry.depth < depth) {
        return 0;
    }

    table_score = search_score_from_table(entry.score, ply);
    if (entry.flag != TRANSPOSITION_EXACT &&
        (exact_only ||
         (entry.flag == TRANSPOSITION_LOWER_BOUND && table_score < beta) ||
         (entry.flag == TRANSPOSITION_UPPER_BOUND && table_score > alpha))) {
        return 0;
    }

    if (score != 0) {
        *score = table_score;
    }
    context->transposition_statistics.score_cutoffs++;
    return 1;
}

int probe_search_transposition_table(
    SearchContext *context,
    uint64_t key,
    int depth,
    int alpha,
    int beta,
    int ply,
    int *score,
    Move *best_move
) {
    return probe_search_transposition_table_mode(
        context,
        key,
        depth,
        alpha,
        beta,
        ply,
        score,
        best_move,
        0,
        0
    );
}

static int has_null_move_material(const Position *position, Color color) {
    Piece queen;
    Piece rook;
    Piece bishop;
    Piece knight;
    if (position == 0 || color == COLOR_NONE) {
        return 0;
    }

    queen = color == COLOR_WHITE ? PIECE_WHITE_QUEEN : PIECE_BLACK_QUEEN;
    rook = color == COLOR_WHITE ? PIECE_WHITE_ROOK : PIECE_BLACK_ROOK;
    bishop = color == COLOR_WHITE ? PIECE_WHITE_BISHOP : PIECE_BLACK_BISHOP;
    knight = color == COLOR_WHITE ? PIECE_WHITE_KNIGHT : PIECE_BLACK_KNIGHT;

    return (position->piece_occupied[queen] |
            position->piece_occupied[rook] |
            position->piece_occupied[bishop] |
            position->piece_occupied[knight]) != 0;
}

static int move_is_quiet(Move move) {
    return (move.flags & (MOVE_FLAG_CAPTURE | MOVE_FLAG_PROMOTION)) == 0;
}

static void rotate_root_move_picker(MovePicker *picker, int offset) {
    Move moves[MAX_MOVES];
    int scores[MAX_MOVES];
    int see_scores[MAX_MOVES];
    unsigned char see_valid[MAX_MOVES];
    unsigned char gives_check[MAX_MOVES];
    unsigned char check_valid[MAX_MOVES];
    int count;
    int index;

    if (picker == 0 || picker->moves == 0 || offset <= 0) {
        return;
    }

    count = picker->moves->count;
    if (count <= 1) {
        return;
    }
    offset %= count;
    if (offset == 0) {
        return;
    }

    for (index = 0; index < count; ++index) {
        int source = (index + offset) % count;

        moves[index] = picker->moves->moves[source];
        scores[index] = picker->scores[source];
        see_scores[index] = picker->see_scores[source];
        see_valid[index] = picker->see_valid[source];
        gives_check[index] = picker->gives_check[source];
        check_valid[index] = picker->check_valid[source];
    }
    for (index = 0; index < count; ++index) {
        picker->moves->moves[index] = moves[index];
        picker->scores[index] = scores[index];
        picker->see_scores[index] = see_scores[index];
        picker->see_valid[index] = see_valid[index];
        picker->gives_check[index] = gives_check[index];
        picker->check_valid[index] = check_valid[index];
    }
    picker->next_index = 0;
}

static int search_piece_value(Piece piece) {
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

static int attacks_valuable_piece(
    const Position *position,
    Piece attacker,
    Piece target
) {
    int attacker_value;
    int target_value;

    if (position == 0 || target == PIECE_NONE ||
        piece_color(target) == piece_color(attacker) ||
        piece_type(target) == PIECE_TYPE_KING) {
        return 0;
    }

    attacker_value = search_piece_value(attacker);
    target_value = search_piece_value(target);
    return target_value > attacker_value || target_value >= ROOK_VALUE;
}

static int pawn_move_attacks_valuable_piece(
    const Position *position,
    int square,
    Color color
) {
    Piece attacker;
    int row;
    int column;
    int direction;
    int file;

    if (position == 0 || color == COLOR_NONE) {
        return 0;
    }

    attacker = position_piece_at(position, square);
    row = square_row(square);
    column = square_column(square);
    direction = color == COLOR_WHITE ? -1 : 1;

    for (file = column - 1; file <= column + 1; file += 2) {
        int target_square = make_square(row + direction, file);

        if (!is_valid_square(target_square)) {
            continue;
        }
        if (attacks_valuable_piece(
                position,
                attacker,
                position_piece_at(position, target_square)
            )) {
            return 1;
        }
    }

    return 0;
}

static int knight_move_attacks_valuable_piece(
    const Position *position,
    int square
) {
    static const int KNIGHT_OFFSETS[8][2] = {
        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
        {1, -2}, {1, 2}, {2, -1}, {2, 1}
    };
    Piece attacker;
    int row;
    int column;
    int index;

    if (position == 0) {
        return 0;
    }

    attacker = position_piece_at(position, square);
    row = square_row(square);
    column = square_column(square);

    for (index = 0; index < 8; ++index) {
        int target_square = make_square(
            row + KNIGHT_OFFSETS[index][0],
            column + KNIGHT_OFFSETS[index][1]
        );

        if (!is_valid_square(target_square)) {
            continue;
        }
        if (attacks_valuable_piece(
                position,
                attacker,
                position_piece_at(position, target_square)
            )) {
            return 1;
        }
    }

    return 0;
}

static int sliding_move_attacks_valuable_piece(
    const Position *position,
    int square,
    const int directions[][2],
    int direction_count
) {
    Piece attacker;
    int start_row;
    int start_column;
    int direction_index;

    if (position == 0) {
        return 0;
    }

    attacker = position_piece_at(position, square);
    start_row = square_row(square);
    start_column = square_column(square);

    for (direction_index = 0;
         direction_index < direction_count;
         ++direction_index) {
        int row = start_row + directions[direction_index][0];
        int column = start_column + directions[direction_index][1];

        while (is_valid_coordinate(row, column)) {
            int target_square = make_square(row, column);
            Piece target = position_piece_at(position, target_square);

            if (target != PIECE_NONE) {
                if (attacks_valuable_piece(position, attacker, target)) {
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

static Piece first_piece_in_direction(
    const Position *position,
    int square,
    int row_step,
    int column_step
) {
    int row;
    int column;

    if (position == 0 || !is_valid_square(square)) {
        return PIECE_NONE;
    }

    row = square_row(square) + row_step;
    column = square_column(square) + column_step;
    while (is_valid_coordinate(row, column)) {
        Piece piece = position_piece_at_coordinates(position, row, column);

        if (piece != PIECE_NONE) {
            return piece;
        }

        row += row_step;
        column += column_step;
    }

    return PIECE_NONE;
}

static int piece_slides_in_direction(
    Piece piece,
    int row_step,
    int column_step
) {
    PieceType type = piece_type(piece);

    if (type == PIECE_TYPE_QUEEN) {
        return 1;
    }

    if (row_step != 0 && column_step != 0) {
        return type == PIECE_TYPE_BISHOP;
    }

    return type == PIECE_TYPE_ROOK;
}

static int quiet_move_reveals_valuable_attack(
    const Position *position,
    int vacated_square,
    Color moving_color
) {
    static const int DIRECTIONS[4][2] = {
        {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}
    };
    int index;

    if (position == 0 || moving_color == COLOR_NONE ||
        !is_valid_square(vacated_square)) {
        return 0;
    }

    for (index = 0; index < 4; ++index) {
        int row_step = DIRECTIONS[index][0];
        int column_step = DIRECTIONS[index][1];
        Piece forward = first_piece_in_direction(
            position,
            vacated_square,
            row_step,
            column_step
        );
        Piece backward = first_piece_in_direction(
            position,
            vacated_square,
            -row_step,
            -column_step
        );

        if (piece_color(forward) == moving_color &&
            piece_slides_in_direction(forward, row_step, column_step) &&
            attacks_valuable_piece(position, forward, backward)) {
            return 1;
        }

        if (piece_color(backward) == moving_color &&
            piece_slides_in_direction(backward, row_step, column_step) &&
            attacks_valuable_piece(position, backward, forward)) {
            return 1;
        }
    }

    return 0;
}

static int quiet_move_attacks_valuable_piece(
    const Position *position,
    Move move,
    Color moving_color
) {
    static const int BISHOP_DIRECTIONS[4][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}
    };
    static const int ROOK_DIRECTIONS[4][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    static const int QUEEN_DIRECTIONS[8][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    Piece attacker;

    if (position == 0 || !move_is_quiet(move) ||
        moving_color == COLOR_NONE) {
        return 0;
    }

    attacker = position_piece_at(position, move.to);
    if (piece_color(attacker) != moving_color) {
        return 0;
    }

    switch (piece_type(attacker)) {
        case PIECE_TYPE_PAWN:
            if (pawn_move_attacks_valuable_piece(
                position,
                move.to,
                moving_color
            )) {
                return 1;
            }
            break;
        case PIECE_TYPE_KNIGHT:
            if (knight_move_attacks_valuable_piece(position, move.to)) {
                return 1;
            }
            break;
        case PIECE_TYPE_BISHOP:
            if (sliding_move_attacks_valuable_piece(
                position,
                move.to,
                BISHOP_DIRECTIONS,
                4
            )) {
                return 1;
            }
            break;
        case PIECE_TYPE_ROOK:
            if (sliding_move_attacks_valuable_piece(
                position,
                move.to,
                ROOK_DIRECTIONS,
                4
            )) {
                return 1;
            }
            break;
        case PIECE_TYPE_QUEEN:
            if (sliding_move_attacks_valuable_piece(
                position,
                move.to,
                QUEEN_DIRECTIONS,
                8
            )) {
                return 1;
            }
            break;
        default:
            break;
    }

    return quiet_move_reveals_valuable_attack(
        position,
        move.from,
        moving_color
    );
}

static int static_futility_margin(int depth, int improving) {
    int margin = effective_static_futility_margin() * depth;

    return improving ? margin + 45 : margin;
}

static int late_move_pruning_threshold(
    int depth,
    int is_pv_node,
    int improving
) {
    int threshold;

    if (!is_pv_node) {
        threshold = (LATE_MOVE_PRUNING_START + depth * depth) /
            (2 - improving);
    } else {
        threshold = LATE_MOVE_PRUNING_START + depth * depth;
    }

    if (is_pv_node) {
        threshold += 3;
    }
    if (improving) {
        threshold += 2;
    }
    if (threshold < 3) {
        threshold = 3;
    }

    return threshold;
}

static int null_move_reduction(int depth, int static_score, int beta) {
    int reduction = effective_null_move_base() + depth / 4;
    int margin = static_score - beta;

    if (margin > 200) {
        reduction++;
    }
    if (margin > 500) {
        reduction++;
    }
    if (reduction > depth - 1) {
        reduction = depth - 1;
    }

    return reduction;
}

static int null_move_static_margin(int depth, int improving) {
    int margin = 365 - depth * 13 - (improving ? 47 : 0);

    return margin > 0 ? margin : 0;
}

static int search_static_evaluation(
    Position *position,
    SearchContext *context,
    int ply,
    const TranspositionEntry *table_entry
) {
    int score;

    if (context != 0) {
        context->static_evaluation_calls++;
    }
    if (context != 0 && table_entry != 0 &&
        table_entry->is_valid && table_entry->has_static_evaluation) {
        score = table_entry->static_evaluation;
        context->static_evaluation_cache_hits++;
        if (ply >= 0 && ply < MAX_SEARCH_PLY) {
            context->static_evaluations[ply] = score;
            context->static_evaluation_valid[ply] = 1;
        }
        return score;
    }
    if (context != 0 &&
        probe_transposition_static_evaluation(
            &context->shared_state->transposition_table,
            position_key(position),
            &score
        )) {
        context->static_evaluation_cache_hits++;
        if (ply >= 0 && ply < MAX_SEARCH_PLY) {
            context->static_evaluations[ply] = score;
            context->static_evaluation_valid[ply] = 1;
        }

        return score;
    }

    if (context != 0) {
        context->raw_evaluations++;
    }
    score = search_evaluate_position(context, position, ply);

    if (context != 0 && ply >= 0 && ply < MAX_SEARCH_PLY) {
        context->static_evaluations[ply] = score;
        context->static_evaluation_valid[ply] = 1;
    }

    return score;
}

static int corrected_static_evaluation(
    Position *position,
    SearchContext *context,
    int ply,
    int raw_score
) {
    int score = raw_score + correction_history_score(
        context,
        position,
        ply
    ) / 2;

    if (context != 0 && ply >= 0 && ply < MAX_SEARCH_PLY) {
        context->static_evaluations[ply] = score;
        context->static_evaluation_valid[ply] = 1;
    }
    return score;
}

static int position_is_improving(
    const SearchContext *context,
    int ply,
    int static_score
) {
    if (context == 0 || ply < 2 || ply >= MAX_SEARCH_PLY ||
        !context->static_evaluation_valid[ply - 2]) {
        return 0;
    }

    return static_score > context->static_evaluations[ply - 2];
}

static int adjusted_late_move_reduction(
    const SearchContext *context,
    const Position *position,
    Color moving_color,
    int ply,
    int depth,
    int move_index,
    int is_pv_node,
    int cut_node,
    int improving,
    Move move,
    int move_history,
    int capture_history
) {
    int reduction = late_move_reduction(depth, move_index);
    int history_score = move_is_quiet(move)
        ? move_history
        : capture_history;

    if (reduction <= 0) {
        return 0;
    }

    if (cut_node) {
        reduction += THETA_CUT_NODE_LMR_BONUS;
    }

    if (is_pv_node) {
        reduction--;
    }
    if (improving) {
        reduction--;
    } else {
        reduction++;
    }

    if (context != 0 && moving_color != COLOR_NONE &&
        history_score == 0) {
        history_score = quiet_history_score(
            context,
            position,
            moving_color,
            ply,
            move
        );
    }

    if (history_score > SEARCH_HISTORY_LIMIT / 3) {
        reduction--;
    } else if (history_score < -SEARCH_HISTORY_LIMIT / 3) {
        reduction++;
    }
    if (history_score > SEARCH_HISTORY_LIMIT * 2) {
        reduction--;
    } else if (history_score < -SEARCH_HISTORY_LIMIT * 2) {
        reduction++;
    }

    if (reduction < 0) {
        reduction = 0;
    }
    if (reduction > depth - 2) {
        reduction = depth - 2;
    }
    if (reduction > 1) {
        reduction--;
    }

    return reduction;
}

static int pawn_reaches_seventh_rank(const Position *position, Move move) {
    Piece piece = position_piece_at(position, move.from);
    int target_row = square_row(move.to);

    if (piece == PIECE_WHITE_PAWN) {
        return target_row == 1;
    }

    if (piece == PIECE_BLACK_PAWN) {
        return target_row == 6;
    }

    return 0;
}

static int negamax(
    Position *position,
    int depth,
    int alpha,
    int beta,
    int ply,
    int allow_null_move,
    int is_pv_node,
    int cut_node,
    PrincipalVariation *variation,
    SearchContext *context,
    const Move *excluded_move
) {
    MoveList moves;
    MovePicker move_picker;
    Move table_move;
    uint64_t key;
    int table_score;
    int in_check;
    int static_score;
    int raw_static_score;
    int pruning_score;
    uint64_t pinned_pieces;
    int table_entry_score = 0;
    int improving;
    int original_alpha = alpha;
    int legal_move_count = 0;
    int searched_move_count = 0;
    int best_score = -SEARCH_INFINITY;
    TranspositionEntry table_entry = {};
    int index;

    clear_variation(variation);
    table_move.from = NO_SQUARE;
    table_move.to = NO_SQUARE;
    table_move.promotion = PIECE_NONE;
    table_move.flags = MOVE_FLAG_NONE;

    search_record_node(context, ply, 0);

    if (search_has_stopped(context)) {
        return 0;
    }

    if (search_is_draw(context, position)) {
        return search_draw_score(context);
    }

    if (ply >= MAX_SEARCH_PLY - 1) {
        if (context != 0) {
            context->raw_evaluations++;
        }
        return search_evaluate_position(context, position, ply);
    }

    if (alpha < -SEARCH_CHECKMATE + ply) {
        alpha = -SEARCH_CHECKMATE + ply;
    }
    if (beta > SEARCH_CHECKMATE - ply - 1) {
        beta = SEARCH_CHECKMATE - ply - 1;
    }
    if (alpha >= beta) {
        return alpha;
    }

    if (depth <= 0) {
        return quiescence_search(position, alpha, beta, ply, context);
    }

#if defined(THETA_EARLY_CHECK_CONTEXT)
    in_check = position_is_in_check(position);
    pinned_pieces = position_pinned_pieces(
        position,
        position->side_to_move
    );
#endif
    key = position_key(position);
    if (excluded_move == 0 && probe_search_transposition_table_mode(
            context,
            key,
            depth,
            alpha,
            beta,
            ply,
            &table_score,
            &table_move,
            is_pv_node,
            &table_entry
        )) {
        if (is_valid_square(table_move.from) && is_valid_square(table_move.to)) {
            update_variation(variation, table_move, 0);
        }
        if (table_entry.flag == TRANSPOSITION_LOWER_BOUND &&
            table_score >= beta &&
            is_valid_square(table_move.from) &&
            is_valid_square(table_move.to)) {
            if (move_is_quiet(table_move)) {
                record_quiet_cutoff(
                    context,
                    position,
                    position->side_to_move,
                    ply,
                    depth,
                    table_move
                );
            } else {
                record_capture_cutoff(
                    context,
                    position,
                    position->side_to_move,
                    depth,
                    table_move
                );
            }
        }

        return table_score;
    }

#if !defined(THETA_EARLY_CHECK_CONTEXT)
    in_check = position_is_in_check(position);
    pinned_pieces = position_pinned_pieces(
        position,
        position->side_to_move
    );
#endif

    raw_static_score = search_static_evaluation(
        position,
        context,
        ply,
        &table_entry
    );
    static_score = corrected_static_evaluation(
        position,
        context,
        ply,
        raw_static_score
    );
    pruning_score = static_score;
    if (table_entry.is_valid) {
        int entry_score = search_score_from_table(table_entry.score, ply);

        if (entry_score < SEARCH_CHECKMATE - MAX_PRINCIPAL_VARIATION &&
            entry_score > -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION &&
            (table_entry.flag == TRANSPOSITION_EXACT ||
             (table_entry.flag == TRANSPOSITION_LOWER_BOUND &&
              entry_score > pruning_score) ||
             (table_entry.flag == TRANSPOSITION_UPPER_BOUND &&
              entry_score < pruning_score))) {
            pruning_score = entry_score;
        }
    }
    improving = position_is_improving(context, ply, static_score);
    if (excluded_move == 0 && !is_pv_node && depth >= 5 &&
        table_entry.is_valid &&
        table_entry.flag == TRANSPOSITION_LOWER_BOUND &&
        table_entry.depth >= depth - 4) {
        table_entry_score = search_score_from_table(
            table_entry.score,
            ply
        );
        if (table_entry_score >= beta + SMALL_PROBCUT_MARGIN &&
            table_entry_score < SEARCH_CHECKMATE -
                MAX_PRINCIPAL_VARIATION) {
            if (context != 0) {
                context->probcut_cutoffs++;
            }
            return beta + SMALL_PROBCUT_MARGIN;
        }
    }

    if (excluded_move == 0 && depth <= 2 && !is_pv_node && !in_check &&
        alpha > -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION &&
        pruning_score + 300 + RAZORING_MARGIN * depth * depth < alpha) {
        int razor_score = quiescence_search(
            position,
            alpha - 1,
            alpha,
            ply,
            context
        );

        if (search_has_stopped(context)) {
            return 0;
        }

        if (razor_score <= alpha) {
            if (context != 0) {
                context->razoring_prunes++;
            }
            return razor_score;
        }
    }

    if (excluded_move == 0 && depth <= 4 && !is_pv_node && !in_check &&
        beta < SEARCH_INFINITY) {
        int margin = depth * REVERSE_FUTILITY_MARGIN +
            (improving ? 0 : 45);

        if (pruning_score - margin >= beta) {
            if (context != 0) {
                context->reverse_futility_prunes++;
            }
            return (pruning_score + beta) / 2;
        }
    }

    if (excluded_move == 0 && allow_null_move && !is_pv_node &&
        depth >= 3 && !in_check &&
        beta < SEARCH_INFINITY &&
        pruning_score >= beta - null_move_static_margin(depth, improving) &&
        has_null_move_material(position, position->side_to_move)) {
        Position null_position = *position;
        PrincipalVariation null_variation;
        int reduction = null_move_reduction(depth, pruning_score, beta);
        int null_score;
        uint64_t null_key = position_key(&null_position);
        int old_en_passant_index = zobrist_en_passant_index(&null_position);

        if (context != 0) {
            context->null_move_attempts++;
        }
        null_position.side_to_move = opposite_color(null_position.side_to_move);
        null_position.en_passant_square = NO_SQUARE;
        null_position.halfmove_clock++;
        null_key ^= zobrist_side_to_move_key();
        null_key ^= zobrist_en_passant_key(old_en_passant_index);
        null_key ^= zobrist_en_passant_key(zobrist_en_passant_index(
            &null_position
        ));
        null_position.zobrist_key = null_key;
        null_position.zobrist_key_valid = 1;
        null_position.zobrist_side_to_move = null_position.side_to_move;
        null_position.zobrist_castling_rights =
            null_position.castling_rights;
        null_position.zobrist_en_passant_square =
            null_position.en_passant_square;
        if (context != 0 && ply >= 0 && ply < MAX_SEARCH_PLY) {
            context->line_moves[ply].from = NO_SQUARE;
            context->line_moves[ply].to = NO_SQUARE;
            context->line_moves[ply].promotion = PIECE_NONE;
            context->line_moves[ply].flags = MOVE_FLAG_NONE;
            context->line_move_types[ply] = PIECE_TYPE_NONE;
        }
        search_push_position(context, &null_position);
        search_copy_nnue_state(context, ply, ply + 1);
        search_copy_classical_state(context, ply, ply + 1);
        null_score = -negamax(
            &null_position,
            depth - 1 - reduction,
            -beta,
            -beta + 1,
            ply + 1,
            0,
            0,
            0,
            &null_variation,
            context,
            0
        );
        search_pop_position(context);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (null_score >= beta &&
            null_score < SEARCH_CHECKMATE - MAX_PRINCIPAL_VARIATION) {
            if (depth >= 12) {
                PrincipalVariation verification_variation;
                int verification_score = negamax(
                    position,
                    depth - reduction,
                    beta - 1,
                    beta,
                    ply,
                    0,
                    0,
                    0,
                    &verification_variation,
                    context,
                    0
                );

                if (search_has_stopped(context)) {
                    return 0;
                }

                if (verification_score < beta) {
                    goto skip_null_cutoff;
                }
            }

            if (context != 0) {
                context->null_move_cutoffs++;
            }
            return null_score;
        }
    }

skip_null_cutoff:
    if (excluded_move == 0 && !is_pv_node && depth >= 5 && !in_check &&
        beta < SEARCH_CHECKMATE - MAX_PRINCIPAL_VARIATION &&
        pruning_score >= beta - 100) {
        MoveList probcut_moves;
        MovePicker probcut_picker;
        Move probcut_move;
        int probcut_beta = beta + 180 - (improving ? 40 : 0);

        context->probcut_attempts++;
        if (context != 0) {
            context->tactical_move_generations++;
        }
        generate_tactical_moves(position, &probcut_moves);
        initialize_move_picker(
            &probcut_picker,
            position,
            &probcut_moves,
            0,
            context,
            ply,
            &table_move
        );

        while (move_picker_next(&probcut_picker, &probcut_move)) {
            PrincipalVariation probcut_variation;
            UndoState probcut_undo;
            int tactical_score;
            int probcut_score;
            int probcut_index = probcut_picker.next_index - 1;

            if ((probcut_move.flags &
                 (MOVE_FLAG_CAPTURE | MOVE_FLAG_PROMOTION)) == 0) {
                continue;
            }
            if ((probcut_move.flags & MOVE_FLAG_PROMOTION) == 0) {
                int see_score;

                if (probcut_picker.see_valid[probcut_index]) {
                    see_score = probcut_picker.see_scores[probcut_index];
                } else {
                    if (context != 0) {
                        context->see_calls++;
                    }
                    see_score = static_exchange_evaluation(
                        position,
                        probcut_move
                    );
                }
                if (see_score < 0) {
                    continue;
                }
            }
            if (context != 0) {
                context->legal_move_attempts++;
            }
            if (!make_legal_move_with_context(
                    position,
                    probcut_move,
                    &probcut_undo,
                    in_check,
                    pinned_pieces
                )) {
                continue;
            }

            search_update_nnue_state(
                context,
                position,
                &probcut_move,
                &probcut_undo,
                ply
            );
            search_prepare_classical_state(
                context,
                &probcut_move,
                &probcut_undo,
                ply + 1
            );

            search_push_position(context, position);
            if (context != 0 && ply >= 0 && ply < MAX_SEARCH_PLY) {
                context->line_moves[ply] = probcut_move;
                context->line_move_types[ply] = piece_type(
                    position_piece_at(position, probcut_move.to)
                );
            }
            tactical_score = -quiescence_search(
                position,
                -probcut_beta,
                -probcut_beta + 1,
                ply + 1,
                context
            );
            probcut_score = tactical_score;
            if (tactical_score >= probcut_beta) {
                probcut_score = -negamax(
                    position,
                    depth - 4,
                    -probcut_beta,
                    -probcut_beta + 1,
                    ply + 1,
                    1,
                    0,
                    !cut_node,
                    &probcut_variation,
                    context,
                    0
                );
            }
            search_pop_position(context);
            undo_move(position, probcut_move, &probcut_undo);

            if (search_has_stopped(context)) {
                return 0;
            }
            if (probcut_score >= probcut_beta) {
                context->probcut_cutoffs++;
                store_transposition_table_with_static_evaluation(
            &context->shared_state->transposition_table,
                    key,
                    depth - 3,
                    search_score_to_table(probcut_score, ply),
                    TRANSPOSITION_LOWER_BOUND,
                    probcut_move,
                    raw_static_score,
                    &context->transposition_statistics
                );
                return probcut_score;
            }
        }
    }

    if (excluded_move == 0 && !is_pv_node && depth >= 5 && !in_check &&
        (!is_valid_square(table_move.from) ||
         !is_valid_square(table_move.to))) {
        depth--;
    }

    /* Singular extension was screened independently and regressed the
     * current search profile. Keep the TT move at the normal depth. */

    if (context != 0) {
        context->move_generations++;
    }
    generate_moves(position, &moves);
    initialize_move_picker(
        &move_picker, position, &moves, 0, context, ply, &table_move
    );

    for (index = 0; move_picker_next(&move_picker, &moves.moves[index]);
         ++index) {
        Move move = moves.moves[index];
        PrincipalVariation child_variation;
        UndoState undo;
        int search_depth = depth - 1;
        int reduced = 0;
        int gives_check;
        int promotes_pawn;
        int creates_threat = 0;
        int quiet_move;
        int see_score = 0;
        int capture_history = 0;
        int move_history = 0;
        Color moving_color;
        PieceType moving_type;
        int move_index;
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        if (excluded_move != 0 &&
            move.from == excluded_move->from &&
            move.to == excluded_move->to &&
            move.promotion == excluded_move->promotion) {
            continue;
        }

        promotes_pawn = pawn_reaches_seventh_rank(position, move);
        quiet_move = move_is_quiet(move);
        moving_color = position->side_to_move;
        moving_type = piece_type(position_piece_at(position, move.from));
        if (quiet_move) {
            move_history = quiet_history_score(
                context,
                position,
                moving_color,
                ply,
                move
            );
        }

        if (!quiet_move &&
            (move.flags & MOVE_FLAG_PROMOTION) == 0 &&
            depth <= 4 && !in_check) {
            if (move_picker.see_valid[index]) {
                see_score = move_picker.see_scores[index];
            } else {
                if (context != 0) {
                    context->see_calls++;
                }
                see_score = static_exchange_evaluation(position, move);
            }
        }

        if (context != 0) {
            context->legal_move_attempts++;
        }
        if (!make_legal_move_with_context(
                position,
                move,
                &undo,
                in_check,
                pinned_pieces
            )) {
            continue;
        }

        move_index = legal_move_count++;
        gives_check = position_is_in_check(position);
        if (!quiet_move) {
            capture_history = capture_history_score(
                context,
                moving_color,
                moving_type,
                move.to,
                piece_type(undo.captured_piece)
            );
        }
        if (quiet_move && !gives_check) {
            creates_threat = quiet_move_attacks_valuable_piece(
                position,
                move,
                moving_color
            );
        }

        if (depth >= 4 && !is_pv_node && !in_check &&
            move_index >= 4 && quiet_move && !gives_check &&
            !promotes_pawn && !creates_threat &&
            move_history < -HISTORY_PRUNE_BASE - depth * 100) {
            if (context != 0) {
                context->late_move_prunes++;
            }
            undo_move(position, move, &undo);
            continue;
        }

        if (depth <= 4 && !is_pv_node && !in_check && move_index > 0 &&
            !quiet_move && (move.flags & MOVE_FLAG_PROMOTION) == 0 &&
            !gives_check &&
                pruning_score + search_piece_value(undo.captured_piece) +
                160 + 180 * depth + capture_history / 16 <= alpha) {
            if (context != 0) {
                context->static_futility_prunes++;
            }
            undo_move(position, move, &undo);
            continue;
        }

        if (depth <= 4 && !is_pv_node && !in_check && move_index > 0 &&
            !quiet_move && (move.flags & MOVE_FLAG_PROMOTION) == 0 &&
            !gives_check &&
             see_score < -70 * depth - capture_history / 32) {
            if (context != 0) {
                context->see_prunes++;
            }
            undo_move(position, move, &undo);
            continue;
        }

        if (depth <= 3 && !in_check &&
            move_index >= late_move_pruning_threshold(
                depth,
                is_pv_node,
                improving
            ) &&
            quiet_move && !gives_check && !promotes_pawn &&
            !creates_threat) {
            if (context != 0) {
                context->late_move_prunes++;
            }
            undo_move(position, move, &undo);
            continue;
        }

        if (depth <= 3 && !is_pv_node && !in_check && move_index > 0 &&
            quiet_move && !gives_check &&
            !promotes_pawn && !creates_threat &&
            alpha > -SEARCH_CHECKMATE + MAX_PRINCIPAL_VARIATION &&
            static_score + static_futility_margin(depth, improving) <= alpha) {
            if (context != 0) {
                context->static_futility_prunes++;
            }
            undo_move(position, move, &undo);
            continue;
        }

        /*
         * Build the incremental NNUE child only after move-ordering and
         * pruning checks.  Most legal moves never reach the recursive search,
         * so doing this earlier paid the full auxiliary-feature update cost
         * for moves that were immediately discarded.
         */
        search_update_nnue_state(context, position, &move, &undo, ply);
        search_prepare_classical_state(
            context,
            &move,
            &undo,
            ply + 1
        );
        search_push_position(context, position);
        if (context != 0 && ply >= 0 && ply < MAX_SEARCH_PLY) {
            context->line_moves[ply] = move;
            context->line_move_types[ply] = moving_type;
        }

        if (depth >= 3 && move_index >= 4 && !gives_check &&
            (quiet_move ||
             (depth >= 4 && move_index >= 6 &&
              (move.flags & MOVE_FLAG_PROMOTION) == 0))) {
            int reduction = adjusted_late_move_reduction(
                context,
                position,
                moving_color,
                ply,
                depth,
                move_index,
                is_pv_node,
                cut_node,
                improving,
                move,
                move_history,
                capture_history
            );

            if (!quiet_move && reduction > 0) {
                reduction--;
            }
            if ((promotes_pawn || creates_threat) && reduction > 0) {
                reduction--;
            }
            if (reduction > 0) {
                search_depth -= reduction;
                reduced = 1;
                context->late_move_reductions++;
            }
        }

        if (gives_check && ply < MAX_CHECK_EXTENSION_PLY) {
            search_depth++;
        } else if (promotes_pawn && ply < MAX_CHECK_EXTENSION_PLY) {
            search_depth++;
        } else if (!quiet_move && depth <= 4 && ply > 0 &&
                   context->line_moves[ply - 1].to == move.to &&
                   see_score >= 0 && ply < MAX_CHECK_EXTENSION_PLY) {
            search_depth++;
        }

        if (move_index == 0) {
            score = -negamax(
                position, search_depth, -beta, -alpha, ply + 1, 1,
                is_pv_node,
                is_pv_node ? 0 : !cut_node,
                &child_variation, context, 0
            );
        } else {
            score = -negamax(
                position, search_depth, -alpha - 1, -alpha, ply + 1, 1,
                0,
                1,
                &child_variation, context, 0
            );

            if (score > alpha && reduced) {
                context->late_move_researches++;
                score = -negamax(
                    position, depth - 1, -alpha - 1, -alpha, ply + 1, 1,
                    0,
                    !cut_node,
                    &child_variation, context, 0
                );
            }

            if (score > alpha && score < beta) {
                score = -negamax(
                    position, depth - 1, -beta, -alpha, ply + 1, 1,
                    is_pv_node,
                    0,
                    &child_variation, context, 0
                );
            }
        }
        search_pop_position(context);
        undo_move(position, move, &undo);

        if (search_has_stopped(context)) {
            return 0;
        }

        searched_move_count++;
        if (score > best_score) {
            best_score = score;
        }

        if (score >= beta) {
            context->beta_cutoffs++;
            if (move_index == 0) {
                context->first_move_beta_cutoffs++;
            }
            record_quiet_failures(
                context,
                position,
                position->side_to_move,
                ply,
                depth,
                &moves,
                index
            );
            record_capture_failures(
                context,
                position,
                position->side_to_move,
                depth,
                &moves,
                index
            );
            if (quiet_move) {
                record_quiet_cutoff(
                    context,
                    position,
                    position->side_to_move,
                    ply,
                    depth,
                    move
                );
            } else {
                record_capture_cutoff(
                    context,
                    position,
                    position->side_to_move,
                    depth,
                    move
                );
            }
            update_variation(variation, move, &child_variation);
            if (excluded_move == 0) {
                store_transposition_table_with_static_evaluation(
            &context->shared_state->transposition_table,
                key,
                depth,
                search_score_to_table(score, ply),
                TRANSPOSITION_LOWER_BOUND,
                move,
                raw_static_score,
                &context->transposition_statistics
                );
            }
            return score;
        }

        if (score > alpha) {
            alpha = score;
            update_variation(variation, move, &child_variation);
        }
    }

    if (legal_move_count == 0) {
        return in_check
            ? -SEARCH_CHECKMATE + ply
            : search_draw_score(context);
    }

    if (searched_move_count == 0) {
        best_score = alpha;
    }

    if (excluded_move == 0) {
        if (depth >= 3 && best_score > original_alpha && best_score < beta) {
            record_correction_history(
                context,
                position,
                ply,
                best_score - raw_static_score
            );
        }
        store_transposition_table_with_static_evaluation(
            &context->shared_state->transposition_table,
            key,
            depth,
            search_score_to_table(best_score, ply),
            best_score <= original_alpha
                ? TRANSPOSITION_UPPER_BOUND
                : TRANSPOSITION_EXACT,
            variation->count > 0 ? variation->moves[0] : table_move,
            raw_static_score,
            &context->transposition_statistics
        );
    }

    return best_score;
}

static int search_position_with_variation(
    Position *position,
    int depth,
    int alpha,
    int beta,
    PrincipalVariation *variation,
    SearchContext *context
) {
    MoveList moves;
    MovePicker move_picker;
    Move table_move;
    uint64_t key;
    int table_score;
    int original_alpha = alpha;
    int best_score = -SEARCH_INFINITY;
    int index;

    clear_variation(variation);
    table_move.from = NO_SQUARE;
    table_move.to = NO_SQUARE;
    table_move.promotion = PIECE_NONE;
    table_move.flags = MOVE_FLAG_NONE;

    if (position == 0) {
        return 0;
    }

    search_record_node(context, 0, 0);

    if (search_has_stopped(context)) {
        return 0;
    }

    if (search_is_draw(context, position)) {
        return search_draw_score(context);
    }

    if (depth <= 0) {
        if (context != 0) {
            context->raw_evaluations++;
        }
        return search_evaluate_position(context, position, 0);
    }

    key = position_key(position);
    if (probe_search_transposition_table_mode(
            context,
            key,
            depth,
            alpha,
            beta,
            0,
            &table_score,
            &table_move,
            1,
            0
        )) {
        if (is_valid_square(table_move.from) && is_valid_square(table_move.to)) {
            update_variation(variation, table_move, 0);
        }

        return table_score;
    }

    if (context != 0) {
        context->move_generations++;
    }
    generate_legal_moves(position, &moves);
    initialize_move_picker(
        &move_picker, position, &moves, 0, context, 0, &table_move
    );
    if (context != 0) {
        rotate_root_move_picker(&move_picker, context->root_move_offset);
    }
    if (moves.count == 0) {
        if (position_is_in_check(position)) {
            return -SEARCH_CHECKMATE;
        }

        return search_draw_score(context);
    }

    for (index = 0; move_picker_next(&move_picker, &moves.moves[index]);
         ++index) {
        Move move = moves.moves[index];
        PrincipalVariation child_variation;
        UndoState undo;
        PieceType moving_type = piece_type(
            position_piece_at(position, move.from)
        );
        int quiet_move = move_is_quiet(move);
        int score;

        if (search_has_stopped(context)) {
            return 0;
        }

        if (!make_move(position, move, &undo)) {
            continue;
        }

        search_update_nnue_state(context, position, &move, &undo, 0);
        search_prepare_classical_state(context, &move, &undo, 1);
        search_push_position(context, position);
        if (context != 0) {
            context->line_moves[0] = move;
            context->line_move_types[0] = moving_type;
        }

        if (index == 0) {
            score = -negamax(
                position,
                depth - 1,
                -beta,
                -alpha,
                1,
                1,
                1,
                0,
                &child_variation,
                context,
                0
            );
        } else {
            score = -negamax(
                position,
                depth - 1,
                -alpha - 1,
                -alpha,
                1,
                1,
                0,
                1,
                &child_variation,
                context,
                0
            );

            if (score > alpha && score < beta) {
                score = -negamax(
                    position,
                    depth - 1,
                    -beta,
                    -alpha,
                    1,
                    1,
                    1,
                    0,
                    &child_variation,
                    context,
                    0
                );
            }
        }
        search_pop_position(context);
        undo_move(position, move, &undo);

        if (search_has_stopped(context)) {
            return 0;
        }

        if (score > best_score) {
            best_score = score;
        }

        if (score >= beta) {
            context->beta_cutoffs++;
            if (index == 0) {
                context->first_move_beta_cutoffs++;
            }
            record_quiet_failures(
                context,
                position,
                position->side_to_move,
                0,
                depth,
                &moves,
                index
            );
            record_capture_failures(
                context,
                position,
                position->side_to_move,
                depth,
                &moves,
                index
            );
            if (quiet_move) {
                record_quiet_cutoff(
                    context,
                    position,
                    position->side_to_move,
                    0,
                    depth,
                    move
                );
            } else {
                record_capture_cutoff(
                    context,
                    position,
                    position->side_to_move,
                    depth,
                    move
                );
            }
            update_variation(variation, move, &child_variation);
            store_transposition_table(
            &context->shared_state->transposition_table,
                key,
                depth,
                search_score_to_table(score, 0),
                TRANSPOSITION_LOWER_BOUND,
                move,
                &context->transposition_statistics
            );
            return score;
        }

        if (score > alpha) {
            alpha = score;
            update_variation(variation, move, &child_variation);
        }
    }

    store_transposition_table(
            &context->shared_state->transposition_table,
        key,
        depth,
        search_score_to_table(best_score, 0),
        best_score <= original_alpha
            ? TRANSPOSITION_UPPER_BOUND
            : TRANSPOSITION_EXACT,
        variation->count > 0 ? variation->moves[0] : table_move,
        &context->transposition_statistics
    );

    return best_score;
}

int search_position_with_state(
    SearchSharedState *shared_state,
    Position *position,
    int depth,
    Move *best_move
) {
    PrincipalVariation variation;
    SearchContext context;
    int score;

    if (shared_state == 0) {
        return 0;
    }

    initialize_search_context(&context, shared_state, 0);
    initialize_lmr_reductions();
    search_push_position(&context, position);
    score = search_position_with_variation(
        position,
        depth,
        -SEARCH_INFINITY,
        SEARCH_INFINITY,
        &variation,
        &context
    );

    if (best_move != 0) {
        best_move->from = NO_SQUARE;
        best_move->to = NO_SQUARE;
        best_move->promotion = PIECE_NONE;
        best_move->flags = MOVE_FLAG_NONE;

        if (variation.count > 0) {
            *best_move = variation.moves[0];
        }
    }

    destroy_search_context(&context);

    return score;
}

int search_position(Position *position, int depth, Move *best_move) {
    SearchSharedState shared_state;
    int score;

    if (!initialize_search_shared_state(&shared_state)) {
        return 0;
    }
    score = search_position_with_state(&shared_state, position, depth, best_move);
    destroy_search_shared_state(&shared_state);
    return score;
}

int search_iterative_with_callback_and_node_limit(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    uint64_t node_limit,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
) {
    SearchLimits limits = {};

    limits.soft_time_ms = time_limit_ms;
    limits.hard_time_ms = time_limit_ms;
    limits.node_limit = node_limit;
    limits.poll_interval = DEFAULT_SEARCH_POLL_INTERVAL;
    return search_iterative_with_limits(
        position,
        maximum_depth,
        &limits,
        best_move,
        variation,
        completed_depth,
        callback,
        user_data
    );
}

static int moves_are_equal(Move first, Move second) {
    return first.from == second.from &&
           first.to == second.to &&
           first.promotion == second.promotion &&
           first.flags == second.flags;
}

int search_iterative_with_state_and_limits(
    SearchSharedState *shared_state,
    Position *position,
    int maximum_depth,
    const SearchLimits *limits,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
) {
    Move move;
    Move previous_best_move;
    PrincipalVariation current_variation;
    SearchContext context;
    int depth;
    int score = 0;
    int completed_score;
    int last_score_delta = 0;
    int previous_iteration_ms = 0;
    int stable_best_move_count = 0;
    int soft_time_limit_ms = limits == 0 ? 0 : limits->soft_time_ms;

    previous_best_move.from = NO_SQUARE;
    previous_best_move.to = NO_SQUARE;
    previous_best_move.promotion = PIECE_NONE;
    previous_best_move.flags = MOVE_FLAG_NONE;

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

    if (shared_state == 0 || position == 0) {
        return 0;
    }

    if (maximum_depth <= 0) {
        return evaluate_position(position);
    }

    initialize_search_context(
        &context,
        shared_state,
        limits == 0 ? 0 : limits->hard_time_ms
    );
    initialize_lmr_reductions();
    search_set_limits(&context, limits);
    if (context.nnue_states != 0) {
        context.nnue_state_active = nnue_state_build(
            position,
            &context.nnue_states[0]
        );
    }
#ifdef THETA_ENABLE_CLASSICAL_STATE
    if (!nnue_is_enabled() && context.classical_states != 0) {
        context.classical_state_active = classical_eval_state_build(
            position,
            &context.classical_states[0]
        );
    }
#endif
    if (limits != 0) {
        search_set_position_history(
            &context,
            limits->game_history,
            limits->game_history_count
        );
    }
    if (context.position_key_count == 0 ||
        context.position_keys[context.position_key_count - 1] !=
            position_key(position)) {
        search_push_position(&context, position);
    }

    if (best_move != 0) {
        MoveList legal_moves;

        context.move_generations++;
        generate_legal_moves(position, &legal_moves);
        if (legal_moves.count > 0) {
            *best_move = legal_moves.moves[0];
        }
    }

    context.raw_evaluations++;
    completed_score = search_evaluate_position(&context, position, 0);

    for (depth = 1; depth <= maximum_depth; ++depth) {
        int iteration_start_ms = search_elapsed_ms(&context);
        int alpha = -SEARCH_INFINITY;
        int beta = SEARCH_INFINITY;
        int window = SEARCH_INFINITY;

        if (depth > 1) {
            window = 18 + depth * 3 + last_score_delta / 3;

            if (window > 160) {
                window = 160;
            }

            alpha = completed_score - window;
            beta = completed_score + window;
        }

        while (1) {
            score = search_position_with_variation(
                position,
                depth,
                alpha,
                beta,
                &current_variation,
                &context
            );

            if (context.stopped) {
                break;
            }

            if (score > alpha && score < beta) {
                break;
            }

            context.aspiration_failures++;
            context.aspiration_researches++;
            if (score <= alpha) {
                beta = alpha;
                alpha = score - window;
                if (alpha < -SEARCH_INFINITY) {
                    alpha = -SEARCH_INFINITY;
                }
            } else {
                alpha = beta - window;
                beta = score + window;
                if (beta > SEARCH_INFINITY) {
                    beta = SEARCH_INFINITY;
                }
            }
            window += window / 2 + 8;
        }

        if (context.stopped) {
            break;
        }

        last_score_delta = score > completed_score
            ? score - completed_score
            : completed_score - score;
        completed_score = score;

        if (current_variation.count > 0) {
            move = current_variation.moves[0];

            if (moves_are_equal(move, previous_best_move)) {
                stable_best_move_count++;
            } else {
                stable_best_move_count = 0;
            }
            previous_best_move = move;

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

        if (callback != 0) {
            SearchStatistics statistics;

            search_get_statistics(&context, &statistics);
            callback(depth, score, &current_variation, &statistics, user_data);
        }

        previous_iteration_ms = search_elapsed_ms(&context) - iteration_start_ms;

        if (soft_time_limit_ms > 0) {
            int elapsed_ms = search_elapsed_ms(&context);
            int allocated_ms = soft_time_limit_ms;
            int hard_time_limit_ms = limits == 0 ? 0 : limits->hard_time_ms;
            int estimated_next_iteration_ms = previous_iteration_ms > 0
                ? previous_iteration_ms * 2
                : 1;

            if (stable_best_move_count == 0) {
                allocated_ms += soft_time_limit_ms / 2;
            } else if (stable_best_move_count == 1) {
                allocated_ms += soft_time_limit_ms / 4;
            }
            if (last_score_delta > 100) {
                allocated_ms += soft_time_limit_ms / 2;
            } else if (last_score_delta > 40) {
                allocated_ms += soft_time_limit_ms / 4;
            }
            if (hard_time_limit_ms > 0 && allocated_ms > hard_time_limit_ms) {
                allocated_ms = hard_time_limit_ms;
            }

            if (elapsed_ms >= allocated_ms ||
                elapsed_ms + estimated_next_iteration_ms > allocated_ms) {
                break;
            }
        }
    }

    destroy_search_context(&context);
    return completed_score;
}

int search_iterative_with_limits(
    Position *position,
    int maximum_depth,
    const SearchLimits *limits,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
) {
    SearchSharedState shared_state;
    int score;

    if (!initialize_search_shared_state(&shared_state)) {
        return 0;
    }
    score = search_iterative_with_state_and_limits(
        &shared_state,
        position,
        maximum_depth,
        limits,
        best_move,
        variation,
        completed_depth,
        callback,
        user_data
    );
    destroy_search_shared_state(&shared_state);
    return score;
}

int search_iterative_with_callback(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth,
    SearchInfoCallback callback,
    void *user_data
) {
    return search_iterative_with_callback_and_node_limit(
        position,
        maximum_depth,
        time_limit_ms,
        0,
        best_move,
        variation,
        completed_depth,
        callback,
        user_data
    );
}

int search_iterative(
    Position *position,
    int maximum_depth,
    int time_limit_ms,
    Move *best_move,
    PrincipalVariation *variation,
    int *completed_depth
) {
    return search_iterative_with_callback(
        position,
        maximum_depth,
        time_limit_ms,
        best_move,
        variation,
        completed_depth,
        0,
        0
    );
}
