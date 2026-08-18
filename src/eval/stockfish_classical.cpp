#include "stockfish_classical.h"

#include <stdint.h>

#include "eval_params.h"
#include "evaluation.h"
#include "src/chess/movegen.h"

#define SF_ALL_ATTACKS (PIECE_TYPE_KING + 1)
#define SF_SCORE_SCALE 2

typedef struct SfScore {
    int mg;
    int eg;
} SfScore;

typedef struct SfEvalContext {
    uint64_t attacks[COLOR_NONE][SF_ALL_ATTACKS + 1];
    uint64_t attack2[COLOR_NONE];
    unsigned char attack_counts[COLOR_NONE][SQUARE_COUNT];
    uint64_t piece_attacks[SQUARE_COUNT];
    uint64_t king_ring[COLOR_NONE];
    uint64_t mobility_area[COLOR_NONE];
    int mobility_mg[COLOR_NONE];
    int mobility_eg[COLOR_NONE];
    int king_attackers_count[COLOR_NONE];
    int king_attackers_weight[COLOR_NONE];
    int king_attacks_count[COLOR_NONE];
    int endgame_weight;
} SfEvalContext;

static SfScore sf_make_score(int mg, int eg) {
    SfScore score = {mg, eg};
    return score;
}

static SfScore sf_add_score(SfScore first, SfScore second) {
    return sf_make_score(first.mg + second.mg, first.eg + second.eg);
}

static SfScore sf_scale_score(SfScore score, int scale) {
    return sf_make_score(score.mg * scale, score.eg * scale);
}

static int sf_taper_score(SfScore score, int endgame_weight) {
    return (
        score.mg * (256 - endgame_weight) + score.eg * endgame_weight
    ) / 256;
}

static int sf_popcount(uint64_t value) {
    return __builtin_popcountll(value);
}

static uint64_t sf_bit(int square) {
    return is_valid_square(square) ? UINT64_C(1) << square : 0;
}

static int sf_piece_phase(Piece piece) {
    switch (piece_type(piece)) {
        case PIECE_TYPE_KNIGHT:
        case PIECE_TYPE_BISHOP:
            return 1;
        case PIECE_TYPE_ROOK:
            return 2;
        case PIECE_TYPE_QUEEN:
            return 4;
        default:
            return 0;
    }
}

static int sf_endgame_weight(const Position *position) {
    const EvalParams *params = current_eval_params();
    uint64_t pieces;
    int phase = 0;

    if (position == 0 || params->max_phase <= 0) {
        return 0;
    }

    pieces = position->occupied;
    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);

        pieces &= pieces - 1;
        phase += sf_piece_phase(position_piece_at(position, square));
    }
    if (phase > params->max_phase) {
        phase = params->max_phase;
    }
    return (params->max_phase - phase) * 256 / params->max_phase;
}

static uint64_t sf_pawn_attacks_from(int square, Color color) {
    int row;
    int column;
    int step;
    uint64_t attacks = 0;

    if (!is_valid_square(square) || color == COLOR_NONE) {
        return 0;
    }

    row = square_row(square);
    column = square_column(square);
    step = color == COLOR_WHITE ? -1 : 1;
    row += step;
    if (is_valid_coordinate(row, column - 1)) {
        attacks |= sf_bit(make_square(row, column - 1));
    }
    if (is_valid_coordinate(row, column + 1)) {
        attacks |= sf_bit(make_square(row, column + 1));
    }
    return attacks;
}

static uint64_t sf_piece_attacks(
    const Position *position,
    int square,
    Piece piece
) {
    PieceType type = piece_type(piece);

    if (type == PIECE_TYPE_PAWN) {
        return sf_pawn_attacks_from(square, piece_color(piece));
    }
    return position_piece_attack_map(position, square, type);
}

/* Stockfish's classical evaluator uses x-ray attacks for bishops and rooks
 * when building its attack tables.  Keep the helper local to this evaluator;
 * the normal move generator should continue to use the ordinary attack map. */
static uint64_t sf_piece_attack_map_with_occupancy(
    int square,
    PieceType type,
    uint64_t occupied
) {
    static const int DIRECTIONS[8][2] = {
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
    if (type == PIECE_TYPE_BISHOP) {
        first_direction = 0;
        last_direction = 3;
    } else if (type == PIECE_TYPE_ROOK) {
        first_direction = 4;
        last_direction = 7;
    } else {
        return 0;
    }

    for (direction = first_direction;
         direction <= last_direction;
         ++direction) {
        int row = square_row(square) + DIRECTIONS[direction][0];
        int column = square_column(square) + DIRECTIONS[direction][1];

        while (is_valid_coordinate(row, column)) {
            int target = make_square(row, column);

            attacks |= sf_bit(target);
            if ((occupied & sf_bit(target)) != 0) {
                break;
            }
            row += DIRECTIONS[direction][0];
            column += DIRECTIONS[direction][1];
        }
    }
    return attacks;
}

static uint64_t sf_line_through(int first, int second) {
    uint64_t line = 0;
    int first_row;
    int first_column;
    int second_row;
    int second_column;
    int row_delta;
    int column_delta;
    int row;
    int column;

    if (!is_valid_square(first) || !is_valid_square(second)) {
        return 0;
    }
    first_row = square_row(first);
    first_column = square_column(first);
    second_row = square_row(second);
    second_column = square_column(second);
    row_delta = first_row - second_row;
    column_delta = first_column - second_column;
    if (row_delta < 0) {
        row_delta = -row_delta;
    }
    if (column_delta < 0) {
        column_delta = -column_delta;
    }
    if (first_row != second_row && first_column != second_column &&
        row_delta != column_delta) {
        return 0;
    }

    for (row = 0; row < BOARD_SIZE; ++row) {
        for (column = 0; column < BOARD_SIZE; ++column) {
            int delta_row = row - first_row;
            int delta_column = column - first_column;

            if (first_row == second_row && row == first_row) {
                line |= sf_bit(make_square(row, column));
            } else if (first_column == second_column &&
                       column == first_column) {
                line |= sf_bit(make_square(row, column));
            } else if (row_delta == column_delta &&
                       (delta_row == delta_column ||
                        delta_row == -delta_column)) {
                line |= sf_bit(make_square(row, column));
            }
        }
    }
    return line;
}

static uint64_t sf_context_piece_attacks(
    const Position *position,
    int square,
    Piece piece,
    uint64_t pinned
) {
    PieceType type = piece_type(piece);
    uint64_t occupied;
    uint64_t attacks;

    if (type == PIECE_TYPE_PAWN || type == PIECE_TYPE_KNIGHT ||
        type == PIECE_TYPE_KING || type == PIECE_TYPE_QUEEN) {
        return sf_piece_attacks(position, square, piece);
    }

    occupied = position->occupied;
    if (type == PIECE_TYPE_BISHOP) {
        occupied &= ~(
            position->piece_occupied[PIECE_WHITE_QUEEN] |
            position->piece_occupied[PIECE_BLACK_QUEEN]
        );
    } else {
        occupied &= ~(
            position->piece_occupied[PIECE_WHITE_QUEEN] |
            position->piece_occupied[PIECE_BLACK_QUEEN] |
            position->piece_occupied[
                piece_color(piece) == COLOR_WHITE
                    ? PIECE_WHITE_ROOK
                    : PIECE_BLACK_ROOK
            ]
        );
    }
    attacks = sf_piece_attack_map_with_occupancy(square, type, occupied);
    if ((pinned & sf_bit(square)) != 0) {
        int king_square = piece_color(piece) == COLOR_WHITE
            ? position->white_king_square
            : position->black_king_square;

        attacks &= sf_line_through(king_square, square);
    }
    return attacks;
}

static uint64_t sf_pawn_span(const Position *position, Color color) {
    Piece pawn = color == COLOR_WHITE
        ? PIECE_WHITE_PAWN
        : PIECE_BLACK_PAWN;
    uint64_t pawns = position->piece_occupied[pawn];
    uint64_t span = 0;

    while (pawns != 0) {
        int square = __builtin_ctzll(pawns);
        int row = square_row(square);
        int column = square_column(square);
        int step = color == COLOR_WHITE ? -1 : 1;

        pawns &= pawns - 1;
        for (row += step; row >= 0 && row < BOARD_SIZE; row += step) {
            if (is_valid_coordinate(row, column - 1)) {
                span |= sf_bit(make_square(row, column - 1));
            }
            if (is_valid_coordinate(row, column + 1)) {
                span |= sf_bit(make_square(row, column + 1));
            }
        }
    }
    return span;
}

static int sf_pawn_has_piece_ahead(
    const Position *position,
    int square,
    Color color,
    int adjacent_files
) {
    int row = square_row(square);
    int column = square_column(square);
    int step = color == COLOR_WHITE ? -1 : 1;
    Piece opposing_pawn = color == COLOR_WHITE
        ? PIECE_BLACK_PAWN
        : PIECE_WHITE_PAWN;

    for (row += step; row >= 0 && row < BOARD_SIZE; row += step) {
        int first_file = adjacent_files ? column - 1 : column;
        int last_file = adjacent_files ? column + 1 : column;
        int file;

        for (file = first_file; file <= last_file; ++file) {
            if (file >= 0 && file < BOARD_SIZE &&
                position_piece_at_coordinates(position, row, file) ==
                    opposing_pawn) {
                return 1;
            }
        }
    }
    return 0;
}

static int sf_pawn_is_passed(
    const Position *position,
    int square,
    Color color
) {
    return !sf_pawn_has_piece_ahead(position, square, color, 1);
}

static int sf_pawn_blocked(
    const Position *position,
    int square,
    Color color
) {
    int row = square_row(square) + (color == COLOR_WHITE ? -1 : 1);

    return row >= 0 && row < BOARD_SIZE &&
           position_piece_at_coordinates(
               position,
               row,
               square_column(square)
           ) != PIECE_NONE;
}

static uint64_t sf_pawn_push_targets(
    const Position *position,
    Color color
) {
    Piece pawn = color == COLOR_WHITE
        ? PIECE_WHITE_PAWN
        : PIECE_BLACK_PAWN;
    uint64_t pawns = position->piece_occupied[pawn];
    uint64_t targets = 0;
    int step = color == COLOR_WHITE ? -1 : 1;
    int starting_row = color == COLOR_WHITE ? 6 : 1;

    while (pawns != 0) {
        int square = __builtin_ctzll(pawns);
        int row = square_row(square);
        int column = square_column(square);
        int front_row = row + step;

        pawns &= pawns - 1;
        if (front_row < 0 || front_row >= BOARD_SIZE ||
            position_piece_at_coordinates(position, front_row, column) !=
                PIECE_NONE) {
            continue;
        }

        targets |= sf_bit(make_square(front_row, column));
        if (row == starting_row) {
            int double_row = row + 2 * step;

            if (double_row >= 0 && double_row < BOARD_SIZE &&
                position_piece_at_coordinates(
                    position,
                    double_row,
                    column
                ) == PIECE_NONE) {
                targets |= sf_bit(make_square(double_row, column));
            }
        }
    }
    return targets;
}

static uint64_t sf_pawn_attacks_for_set(
    uint64_t pawns,
    Color color
) {
    uint64_t attacks = 0;

    while (pawns != 0) {
        int square = __builtin_ctzll(pawns);

        pawns &= pawns - 1;
        attacks |= sf_pawn_attacks_from(square, color);
    }
    return attacks;
}

static uint64_t sf_center_files(void) {
    uint64_t files = 0;
    int row;

    for (row = 0; row < BOARD_SIZE; ++row) {
        files |= sf_bit(make_square(row, 2));
        files |= sf_bit(make_square(row, 3));
        files |= sf_bit(make_square(row, 4));
        files |= sf_bit(make_square(row, 5));
    }
    return files;
}

static uint64_t sf_rows_mask(int first_row, int last_row) {
    uint64_t mask = 0;
    int row;

    for (row = first_row; row <= last_row; ++row) {
        int column;

        for (column = 0; column < BOARD_SIZE; ++column) {
            mask |= sf_bit(make_square(row, column));
        }
    }
    return mask;
}

static int sf_front_piece_blocked(
    const Position *position,
    int square,
    Color color
) {
    int row = square_row(square) + (color == COLOR_WHITE ? -1 : 1);

    return row >= 0 && row < BOARD_SIZE &&
           position_piece_at_coordinates(
               position,
               row,
               square_column(square)
           ) != PIECE_NONE;
}

static uint64_t sf_mobility_area(
    const Position *position,
    Color color
) {
    Piece king = color == COLOR_WHITE ? PIECE_WHITE_KING : PIECE_BLACK_KING;
    Piece queen = color == COLOR_WHITE ? PIECE_WHITE_QUEEN : PIECE_BLACK_QUEEN;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    uint64_t pawns = position->piece_occupied[pawn];
    uint64_t blocked = 0;
    uint64_t low_ranks = color == COLOR_WHITE
        ? sf_rows_mask(5, 6)
        : sf_rows_mask(1, 2);

    while (pawns != 0) {
        int square = __builtin_ctzll(pawns);

        pawns &= pawns - 1;
        if (sf_front_piece_blocked(position, square, color)) {
            blocked |= sf_bit(square);
        }
    }

    return ~(blocked |
             (position->piece_occupied[pawn] & low_ranks) |
             position->piece_occupied[king] |
             position->piece_occupied[queen] |
             position_pinned_pieces(position, color) |
             position_pawn_attack_map(position, opposite_color(color)));
}

static uint64_t sf_king_ring(const Position *position, Color color) {
    int king_square = color == COLOR_WHITE
        ? position->white_king_square
        : position->black_king_square;
    uint64_t ring = 0;
    int row;
    int column;
    int center_row;
    int center_column;

    if (!is_valid_square(king_square)) {
        return 0;
    }

    center_row = square_row(king_square);
    center_column = square_column(king_square);
    if (center_row < 1) {
        center_row = 1;
    } else if (center_row > 6) {
        center_row = 6;
    }
    if (center_column < 1) {
        center_column = 1;
    } else if (center_column > 6) {
        center_column = 6;
    }

    for (row = center_row - 1;
         row <= center_row + 1;
         ++row) {
        for (column = center_column - 1;
             column <= center_column + 1;
             ++column) {
            if (is_valid_coordinate(row, column)) {
                ring |= sf_bit(make_square(row, column));
            }
        }
    }
    return ring;
}

static void sf_build_context(
    const Position *position,
    const ClassicalEvalState *state,
    SfEvalContext *context
) {
    uint64_t pieces;
    uint64_t pinned[COLOR_NONE] = {};
    int color;

    *context = (SfEvalContext){};
    context->endgame_weight = sf_endgame_weight(position);
    for (color = COLOR_WHITE; color <= COLOR_BLACK; ++color) {
        context->king_ring[color] = sf_king_ring(position, (Color)color);
        context->mobility_area[color] = sf_mobility_area(
            position,
            (Color)color
        );
        pinned[color] = position_pinned_pieces(position, (Color)color);
    }

    pieces = position->occupied;
    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        Piece piece;
        PieceType type;
        Color color;
        uint64_t attacks;

        pieces &= pieces - 1;
        piece = position_piece_at(position, square);
        type = piece_type(piece);
        color = piece_color(piece);
        if (color == COLOR_NONE || type == PIECE_TYPE_NONE) {
            continue;
        }
        (void)state;
        attacks = sf_context_piece_attacks(
            position,
            square,
            piece,
            pinned[color]
        );
        context->piece_attacks[square] = attacks;
        context->attacks[color][type] |= attacks;
        context->attacks[color][SF_ALL_ATTACKS] |= attacks;
        while (attacks != 0) {
            int target = __builtin_ctzll(attacks);

            attacks &= attacks - 1;
            if (context->attack_counts[color][target] != 255) {
                context->attack_counts[color][target]++;
            }
        }
    }

    for (color = COLOR_WHITE; color <= COLOR_BLACK; ++color) {
        int square;

        for (square = 0; square < SQUARE_COUNT; ++square) {
            if (context->attack_counts[color][square] >= 2) {
                context->attack2[color] |= sf_bit(square);
            }
        }
    }

    for (color = COLOR_WHITE; color <= COLOR_BLACK; ++color) {
        uint64_t own_pieces = position->color_occupied[color];

        while (own_pieces != 0) {
            int square = __builtin_ctzll(own_pieces);
            Piece piece;
            PieceType type;
            uint64_t attacks;
            Color enemy = opposite_color((Color)color);

            own_pieces &= own_pieces - 1;
            piece = position_piece_at(position, square);
            type = piece_type(piece);
            attacks = context->piece_attacks[square];
            if ((attacks & context->king_ring[enemy]) != 0) {
                context->king_attackers_count[color]++;
                context->king_attackers_weight[color] +=
                    type == PIECE_TYPE_KNIGHT ? 81 :
                    type == PIECE_TYPE_BISHOP ? 52 :
                    type == PIECE_TYPE_ROOK ? 44 :
                    type == PIECE_TYPE_QUEEN ? 10 : 0;
                context->king_attacks_count[color] += sf_popcount(
                    attacks & context->king_ring[enemy]
                );
            }
        }
    }

    {
        static const int mobility_mg[4][28] = {
            {-62,-53,-12,-4,3,13,22,28,33},
            {-48,-20,16,26,38,51,55,63,63,68,81,81,91,98},
            {-58,-27,-15,-10,-5,-2,9,16,30,29,32,38,46,48,58},
            {-39,-21,3,3,14,22,28,41,43,48,56,60,60,66,67,70,71,73,
             79,88,88,99,102,102,106,109,113,116}
        };
        static const int mobility_eg[4][28] = {
            {-81,-56,-30,-14,8,15,23,27,33},
            {-59,-23,-3,13,24,42,54,57,65,73,78,86,88,97},
            {-76,-18,28,55,69,82,112,118,132,142,155,165,166,169,171},
            {-36,-15,8,18,34,54,61,73,79,92,94,104,113,120,123,126,
             133,136,140,143,148,166,170,175,184,191,206,212}
        };
        uint64_t all_pieces = position->occupied;

        while (all_pieces != 0) {
            int square = __builtin_ctzll(all_pieces);
            Piece piece;
            PieceType type;
            Color piece_color_value;
            int table;
            int mobility;

            all_pieces &= all_pieces - 1;
            piece = position_piece_at(position, square);
            type = piece_type(piece);
            piece_color_value = piece_color(piece);
            if (type < PIECE_TYPE_KNIGHT ||
                type > PIECE_TYPE_QUEEN) {
                continue;
            }
            table = type - PIECE_TYPE_KNIGHT;
            mobility = sf_popcount(
                context->piece_attacks[square] &
                context->mobility_area[piece_color_value] &
                ~position->color_occupied[piece_color_value]
            );
            if (mobility > 27) {
                mobility = 27;
            }
            context->mobility_mg[piece_color_value] +=
                mobility_mg[table][mobility];
            context->mobility_eg[piece_color_value] +=
                mobility_eg[table][mobility];
        }
    }
}

static int sf_distance(int first, int second) {
    int row = square_row(first) - square_row(second);
    int column = square_column(first) - square_column(second);

    if (row < 0) {
        row = -row;
    }
    if (column < 0) {
        column = -column;
    }
    return row > column ? row : column;
}

/*
 * Stockfish's classical evaluator used a second-degree material model in
 * addition to the nominal piece values.  This captures exchanges such as
 * bishop pair versus two knights and queen/minor imbalances that a purely
 * linear material count cannot distinguish.
 */
static SfScore sf_material_imbalance(const Position *position) {
    static const SfScore quadratic_ours[6][6] = {
        {{1419, 1455}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
        {{101, 28}, {37, 39}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
        {{57, 64}, {249, 187}, {-49, -62}, {0, 0}, {0, 0}, {0, 0}},
        {{0, 0}, {118, 137}, {10, 27}, {0, 0}, {0, 0}, {0, 0}},
        {{-63, -68}, {-5, 3}, {100, 81}, {132, 118},
         {-246, -244}, {0, 0}},
        {{-210, -211}, {37, 14}, {147, 141}, {161, 105},
         {-158, -174}, {-9, -31}}
    };
    static const SfScore quadratic_theirs[6][6] = {
        {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
        {{33, 30}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
        {{46, 18}, {106, 84}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
        {{75, 35}, {59, 44}, {60, 15}, {0, 0}, {0, 0}, {0, 0}},
        {{26, 35}, {6, 22}, {38, 39}, {-12, -2},
         {0, 0}, {0, 0}},
        {{97, 93}, {100, 163}, {-58, -91}, {112, 192},
         {276, 225}, {0, 0}}
    };
    int counts[COLOR_NONE][6] = {};
    SfScore result = {0, 0};
    int color;

    if (position == 0) {
        return result;
    }

    counts[COLOR_WHITE][0] = __builtin_popcountll(
        position->piece_occupied[PIECE_WHITE_BISHOP]
    ) > 1;
    counts[COLOR_BLACK][0] = __builtin_popcountll(
        position->piece_occupied[PIECE_BLACK_BISHOP]
    ) > 1;
    for (color = COLOR_WHITE; color <= COLOR_BLACK; ++color) {
        Piece pieces_by_type[5] = {
            color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN,
            color == COLOR_WHITE ? PIECE_WHITE_KNIGHT : PIECE_BLACK_KNIGHT,
            color == COLOR_WHITE ? PIECE_WHITE_BISHOP : PIECE_BLACK_BISHOP,
            color == COLOR_WHITE ? PIECE_WHITE_ROOK : PIECE_BLACK_ROOK,
            color == COLOR_WHITE ? PIECE_WHITE_QUEEN : PIECE_BLACK_QUEEN
        };
        int type;

        for (type = 0; type < 5; ++type) {
            counts[color][type + 1] = __builtin_popcountll(
                position->piece_occupied[pieces_by_type[type]]
            );
        }
    }

    for (color = COLOR_WHITE; color <= COLOR_BLACK; ++color) {
        Color enemy = opposite_color((Color)color);
        SfScore side = {0, 0};
        int first_type;

        for (first_type = 0; first_type < 6; ++first_type) {
            int first_count = counts[color][first_type];
            SfScore value;
            int second_type;

            if (first_count == 0) {
                continue;
            }
            value = sf_scale_score(
                quadratic_ours[first_type][first_type],
                first_count
            );
            for (second_type = 0; second_type < first_type;
                 ++second_type) {
                value = sf_add_score(
                    value,
                    sf_scale_score(
                        quadratic_ours[first_type][second_type],
                        counts[color][second_type]
                    )
                );
                value = sf_add_score(
                    value,
                    sf_scale_score(
                        quadratic_theirs[first_type][second_type],
                        counts[enemy][second_type]
                    )
                );
            }
            side = sf_add_score(
                side,
                sf_scale_score(value, first_count)
            );
        }
        if (color == COLOR_WHITE) {
            result = sf_add_score(result, side);
        } else {
            result = sf_make_score(
                result.mg - side.mg,
                result.eg - side.eg
            );
        }
    }

    return sf_make_score(result.mg / 16, result.eg / 16);
}

static int sf_pawns_on_square_color(
    const Position *position,
    Color color,
    int square_color
) {
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    uint64_t pawns = position->piece_occupied[pawn];
    int count = 0;

    while (pawns != 0) {
        int square = __builtin_ctzll(pawns);

        pawns &= pawns - 1;
        if (((square_row(square) + square_column(square)) & 1) ==
            square_color) {
            count++;
        }
    }
    return count;
}

static int sf_behind_own_pawn(
    const Position *position,
    int square,
    Color color
) {
    int pawn_row = square_row(square) + (color == COLOR_WHITE ? -1 : 1);

    return pawn_row >= 0 && pawn_row < BOARD_SIZE &&
           position_piece_at_coordinates(
               position,
               pawn_row,
               square_column(square)
           ) == (color == COLOR_WHITE
               ? PIECE_WHITE_PAWN
               : PIECE_BLACK_PAWN);
}

static int sf_is_outpost(
    int square,
    Color color,
    uint64_t own_pawn_attacks,
    uint64_t enemy_pawn_span
) {
    int row = square_row(square);

    if (color == COLOR_WHITE) {
        if (row < 2 || row > 4) {
            return 0;
        }
    } else if (row < 3 || row > 5) {
        return 0;
    }
    return (own_pawn_attacks & sf_bit(square)) != 0 &&
           (enemy_pawn_span & sf_bit(square)) == 0;
}

static SfScore sf_piece_score(
    const Position *position,
    const SfEvalContext *context,
    Color color
) {
    static const SfScore outpost = {30, 21};
    static const SfScore reachable_outpost = {32, 10};
    static const SfScore minor_behind_pawn = {18, 3};
    static const SfScore king_protector = {7, 8};
    static const SfScore bishop_pawns = {3, 7};
    static const SfScore bishop_on_king_ring = {24, 0};
    static const SfScore rook_on_king_ring = {16, 0};
    static const SfScore bishop_xray_pawns = {4, 5};
    static const SfScore long_diagonal_bishop = {45, 0};
    static const SfScore rook_on_queen_file = {7, 6};
    static const SfScore rook_on_file[2] = {{21, 4}, {47, 25}};
    static const SfScore trapped_rook = {52, 10};
    static const SfScore weak_queen = {56, 15};
    SfScore score = {0, 0};
    uint64_t pieces = position->color_occupied[color];
    uint64_t own_pawn_attacks = position_pawn_attack_map(position, color);
    uint64_t enemy_pawn_span = sf_pawn_span(
        position,
        opposite_color(color)
    );
    int own_king = color == COLOR_WHITE
        ? position->white_king_square
        : position->black_king_square;
    uint64_t own_pinned = position_pinned_pieces(position, color);
    uint64_t all_pawns = position->piece_occupied[PIECE_WHITE_PAWN] |
        position->piece_occupied[PIECE_BLACK_PAWN];

    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        Piece piece;
        PieceType type;
        uint64_t attacks;

        pieces &= pieces - 1;
        piece = position_piece_at(position, square);
        type = piece_type(piece);
        attacks = context->piece_attacks[square];
        if (type != PIECE_TYPE_BISHOP && type != PIECE_TYPE_KNIGHT &&
            type != PIECE_TYPE_ROOK && type != PIECE_TYPE_QUEEN) {
            continue;
        }

        if ((attacks & context->king_ring[opposite_color(color)]) == 0) {
            if (type == PIECE_TYPE_ROOK &&
                (context->king_ring[opposite_color(color)] &
                 (UINT64_C(0x0101010101010101) << square_column(square))) !=
                    0) {
                score = sf_add_score(score, rook_on_king_ring);
            } else if (type == PIECE_TYPE_BISHOP &&
                       (sf_piece_attack_map_with_occupancy(
                            square,
                            PIECE_TYPE_BISHOP,
                            all_pawns
                        ) & context->king_ring[opposite_color(color)]) != 0) {
                score = sf_add_score(score, bishop_on_king_ring);
            }
        }

        if (type == PIECE_TYPE_BISHOP || type == PIECE_TYPE_KNIGHT) {
            if (sf_is_outpost(
                    square,
                    color,
                    own_pawn_attacks,
                    enemy_pawn_span
                )) {
                SfScore value = type == PIECE_TYPE_KNIGHT
                    ? sf_scale_score(outpost, 2)
                    : outpost;
                score = sf_add_score(score, value);
            } else if (type == PIECE_TYPE_KNIGHT &&
                       (attacks & own_pawn_attacks) != 0) {
                score = sf_add_score(score, reachable_outpost);
            }

            if (sf_behind_own_pawn(position, square, color)) {
                score = sf_add_score(score, minor_behind_pawn);
            }
            if (is_valid_square(own_king)) {
                score.mg -= king_protector.mg * sf_distance(square, own_king);
                score.eg -= king_protector.eg * sf_distance(square, own_king);
            }

            if (type == PIECE_TYPE_BISHOP) {
                int same_color = sf_pawns_on_square_color(
                    position,
                    color,
                    (square_row(square) + square_column(square)) & 1
                );
                int blocked_center = 0;
                uint64_t pawns = position->piece_occupied[
                    color == COLOR_WHITE ? PIECE_WHITE_PAWN
                                         : PIECE_BLACK_PAWN
                ];

                while (pawns != 0) {
                    int pawn_square = __builtin_ctzll(pawns);

                    pawns &= pawns - 1;
                    if (sf_front_piece_blocked(
                            position,
                            pawn_square,
                            color
                        ) && square_column(pawn_square) >= 2 &&
                        square_column(pawn_square) <= 5) {
                        blocked_center++;
                    }
                }
                score.mg -= bishop_pawns.mg * same_color *
                    (1 + blocked_center);
                score.eg -= bishop_pawns.eg * same_color *
                    (1 + blocked_center);
                score.mg -= bishop_xray_pawns.mg * sf_popcount(
                    sf_piece_attack_map_with_occupancy(
                        square,
                        PIECE_TYPE_BISHOP,
                        0
                    ) & position->color_occupied[opposite_color(color)] &
                    (position->piece_occupied[
                        opposite_color(color) == COLOR_WHITE
                            ? PIECE_WHITE_PAWN
                            : PIECE_BLACK_PAWN
                    ])
                );
                score.eg -= bishop_xray_pawns.eg * sf_popcount(
                    sf_piece_attack_map_with_occupancy(
                        square,
                        PIECE_TYPE_BISHOP,
                        0
                    ) & position->color_occupied[opposite_color(color)] &
                    (position->piece_occupied[
                        opposite_color(color) == COLOR_WHITE
                            ? PIECE_WHITE_PAWN
                            : PIECE_BLACK_PAWN
                    ])
                );
                if (sf_popcount(attacks & sf_center_files()) > 1) {
                    score = sf_add_score(score, long_diagonal_bishop);
                }
            }
        } else if (type == PIECE_TYPE_ROOK) {
            int column = square_column(square);
            Piece own_pawn = color == COLOR_WHITE
                ? PIECE_WHITE_PAWN
                : PIECE_BLACK_PAWN;
            Piece enemy_pawn = color == COLOR_WHITE
                ? PIECE_BLACK_PAWN
                : PIECE_WHITE_PAWN;
            int own_on_file = (
                position->piece_occupied[own_pawn] &
                (UINT64_C(0x0101010101010101) << column)
            ) != 0;
            int enemy_on_file = (
                position->piece_occupied[enemy_pawn] &
                (UINT64_C(0x0101010101010101) << column)
            ) != 0;
            uint64_t queens = position->piece_occupied[
                color == COLOR_WHITE ? PIECE_WHITE_QUEEN
                                     : PIECE_BLACK_QUEEN
            ];
            int mobility = sf_popcount(
                attacks & context->mobility_area[color] &
                ~position->color_occupied[color]
            );

            if (queens & (UINT64_C(0x0101010101010101) << column)) {
                score = sf_add_score(score, rook_on_queen_file);
            }
            if (!own_on_file) {
                score = sf_add_score(score, rook_on_file[!enemy_on_file]);
            } else if (mobility <= 3) {
                int king_column = square_column(own_king);

                if (is_valid_square(own_king) &&
                    (king_column < 4) == (column < king_column)) {
                    score.mg -= trapped_rook.mg;
                    score.eg -= trapped_rook.eg;
                }
            }
        } else if (type == PIECE_TYPE_QUEEN) {
            if ((own_pinned & sf_bit(square)) != 0) {
                score.mg -= weak_queen.mg;
                score.eg -= weak_queen.eg;
            }
        }
    }
    if (sf_popcount(position->piece_occupied[
            color == COLOR_WHITE ? PIECE_WHITE_BISHOP
                                 : PIECE_BLACK_BISHOP
        ]) >= 2) {
        score.mg += 3;
        score.eg += 7;
    }
    return score;
}

static SfScore sf_threat_score(
    const Position *position,
    const SfEvalContext *context,
    Color color
) {
    static const SfScore threat_by_minor[PIECE_TYPE_KING + 1] = {
        {0,0}, {6,32}, {59,41}, {79,56}, {90,119}, {79,161}, {24,89}
    };
    static const SfScore threat_by_rook[PIECE_TYPE_KING + 1] = {
        {0,0}, {3,44}, {38,71}, {38,61}, {0,38}, {51,38}, {0,0}
    };
    static const SfScore hanging = {69, 36};
    static const SfScore weak_queen_protection = {14, 0};
    static const SfScore restricted = {7, 7};
    static const SfScore safe_pawn = {173, 94};
    static const SfScore pawn_push = {48, 39};
    static const SfScore knight_queen = {16, 12};
    static const SfScore slider_queen = {59, 18};
    Color enemy = opposite_color(color);
    Piece enemy_pawn = enemy == COLOR_WHITE
        ? PIECE_WHITE_PAWN
        : PIECE_BLACK_PAWN;
    uint64_t enemy_nonpawns = position->color_occupied[enemy] &
        ~position->piece_occupied[enemy_pawn];
    uint64_t strongly_protected = context->attacks[enemy][PIECE_TYPE_PAWN] |
        (context->attack2[enemy] & ~context->attack2[color]);
    uint64_t defended = enemy_nonpawns & strongly_protected;
    uint64_t weak = position->color_occupied[enemy] &
        ~strongly_protected & context->attacks[color][SF_ALL_ATTACKS];
    uint64_t safe = ~context->attacks[enemy][SF_ALL_ATTACKS] |
        context->attacks[color][SF_ALL_ATTACKS];
    SfScore score = {0, 0};
    uint64_t targets;

    targets = (defended | weak) & (
        context->attacks[color][PIECE_TYPE_KNIGHT] |
        context->attacks[color][PIECE_TYPE_BISHOP]
    );
    while (targets != 0) {
        int square = __builtin_ctzll(targets);
        PieceType type = piece_type(position_piece_at(position, square));

        targets &= targets - 1;
        score = sf_add_score(score, threat_by_minor[type]);
    }

    targets = weak & context->attacks[color][PIECE_TYPE_ROOK];
    while (targets != 0) {
        int square = __builtin_ctzll(targets);
        PieceType type = piece_type(position_piece_at(position, square));

        targets &= targets - 1;
        score = sf_add_score(score, threat_by_rook[type]);
    }
    if ((weak & context->attacks[color][PIECE_TYPE_KING]) != 0) {
        score = sf_add_score(score, threat_by_minor[PIECE_TYPE_KING]);
    }

    score = sf_add_score(
        score,
        sf_scale_score(
            hanging,
            sf_popcount(
                weak & (~context->attacks[enemy][SF_ALL_ATTACKS] |
                    (enemy_nonpawns & context->attack2[color]))
            )
        )
    );
    score = sf_add_score(
        score,
        sf_scale_score(
            weak_queen_protection,
            sf_popcount(weak & context->attacks[enemy][PIECE_TYPE_QUEEN])
        )
    );
    score = sf_add_score(
        score,
        sf_scale_score(
            restricted,
            sf_popcount(
                context->attacks[enemy][SF_ALL_ATTACKS] &
                ~strongly_protected &
                context->attacks[color][SF_ALL_ATTACKS]
            )
        )
    );

    {
        Piece own_pawn = color == COLOR_WHITE
            ? PIECE_WHITE_PAWN
            : PIECE_BLACK_PAWN;
        uint64_t pawns = position->piece_occupied[own_pawn];
        uint64_t safe_pawns = 0;

        while (pawns != 0) {
            int square = __builtin_ctzll(pawns);

            pawns &= pawns - 1;
            if ((safe & sf_bit(square)) != 0) {
                safe_pawns |= sf_bit(square);
            }
        }
        score = sf_add_score(
            score,
            sf_scale_score(
                safe_pawn,
                sf_popcount(
                    sf_pawn_attacks_for_set(safe_pawns, color) &
                    enemy_nonpawns
                )
            )
        );
    }

    targets = sf_pawn_push_targets(position, color);
    targets &= ~context->attacks[enemy][PIECE_TYPE_PAWN] & safe;
    if (targets != 0) {
        uint64_t pawn_attack_targets = 0;
        uint64_t pushes = targets;

        while (pushes != 0) {
            int square = __builtin_ctzll(pushes);

            pushes &= pushes - 1;
            pawn_attack_targets |= sf_pawn_attacks_from(square, color);
        }
        score = sf_add_score(
            score,
            sf_scale_score(
                pawn_push,
                sf_popcount(pawn_attack_targets & enemy_nonpawns)
            )
        );
    }

    if (sf_popcount(position->piece_occupied[
            enemy == COLOR_WHITE ? PIECE_WHITE_QUEEN
                                 : PIECE_BLACK_QUEEN
        ]) == 1) {
        int queen_square = __builtin_ctzll(position->piece_occupied[
            enemy == COLOR_WHITE ? PIECE_WHITE_QUEEN
                                 : PIECE_BLACK_QUEEN
        ]);
        uint64_t queen_knight_attacks = position_piece_attack_map(
            position,
            queen_square,
            PIECE_TYPE_KNIGHT
        );
        uint64_t queen_slider_attacks =
            position_piece_attack_map(
                position,
                queen_square,
                PIECE_TYPE_BISHOP
            ) |
            position_piece_attack_map(
                position,
                queen_square,
                PIECE_TYPE_ROOK
            );

        score = sf_add_score(
            score,
            sf_scale_score(
                knight_queen,
                sf_popcount(
                    context->attacks[color][PIECE_TYPE_KNIGHT] &
                    queen_knight_attacks &
                    context->mobility_area[color]
                )
            )
        );
        score = sf_add_score(
            score,
            sf_scale_score(
                slider_queen,
                sf_popcount(
                    (context->attacks[color][PIECE_TYPE_BISHOP] |
                     context->attacks[color][PIECE_TYPE_ROOK]) &
                    queen_slider_attacks &
                    context->attack2[color]
                )
            )
        );
    }

    return score;
}

static SfScore sf_passed_score(
    const Position *position,
    const SfEvalContext *context,
    Color color
) {
    static const int passed_mg[7] = {0, 10, 17, 15, 62, 168, 276};
    static const int passed_eg[7] = {0, 28, 33, 41, 72, 177, 260};
    static const int passed_file[8] = {0, 1, 2, 3, 3, 2, 1, 0};
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    uint64_t pawns = position->piece_occupied[pawn];
    Color enemy = opposite_color(color);
    int own_king = color == COLOR_WHITE
        ? position->white_king_square
        : position->black_king_square;
    int enemy_king = enemy == COLOR_WHITE
        ? position->white_king_square
        : position->black_king_square;
    SfScore score = {0, 0};

    while (pawns != 0) {
        int square = __builtin_ctzll(pawns);
        int row;
        int relative_rank;
        int step = color == COLOR_WHITE ? -1 : 1;
        int block_square;
        int own_distance;
        int enemy_distance;
        SfScore bonus;

        pawns &= pawns - 1;
        if (!sf_pawn_is_passed(position, square, color)) {
            continue;
        }

        row = square_row(square);
        relative_rank = color == COLOR_WHITE ? 7 - row : row + 1;
        if (relative_rank < 1) {
            relative_rank = 1;
        } else if (relative_rank > 6) {
            relative_rank = 6;
        }
        bonus = sf_make_score(
            passed_mg[relative_rank],
            passed_eg[relative_rank]
        );
        bonus.mg -= passed_file[square_column(square)] * 11;
        bonus.eg -= passed_file[square_column(square)] * 8;

        if (relative_rank > 3) {
            int weight = 5 * relative_rank - 13;
            int block_row = row + step;
            uint64_t squares_to_queen = 0;
            uint64_t unsafe_squares = 0;
            int path_bonus = 0;
            int next_row;

            block_square = is_valid_coordinate(
                block_row,
                square_column(square)
            ) ? make_square(block_row, square_column(square)) : NO_SQUARE;
            for (next_row = block_row;
                 next_row >= 0 && next_row < BOARD_SIZE;
                 next_row += step) {
                int file;

                squares_to_queen |= sf_bit(
                    make_square(next_row, square_column(square))
                );
                for (file = square_column(square) - 1;
                     file <= square_column(square) + 1;
                     ++file) {
                    if (file >= 0 && file < BOARD_SIZE) {
                        unsafe_squares |= sf_bit(make_square(next_row, file));
                    }
                }
            }
            unsafe_squares &= context->attacks[enemy][SF_ALL_ATTACKS];
            if (unsafe_squares == 0) {
                path_bonus = 35;
            } else if ((unsafe_squares & squares_to_queen) == 0) {
                path_bonus = 20;
            } else if (block_square != NO_SQUARE &&
                       (unsafe_squares & sf_bit(block_square)) == 0) {
                path_bonus = 9;
            }
            if (block_square != NO_SQUARE &&
                (context->attacks[color][SF_ALL_ATTACKS] &
                 sf_bit(block_square)) != 0) {
                path_bonus += 5;
            }
            bonus.mg += path_bonus * weight;
            bonus.eg += path_bonus * weight;
            if (block_square != NO_SQUARE && is_valid_square(own_king) &&
                is_valid_square(enemy_king)) {
                own_distance = sf_distance(own_king, block_square);
                enemy_distance = sf_distance(enemy_king, block_square);
                bonus.eg += ((enemy_distance * 19) / 4 -
                             own_distance * 2) * weight;
            }
        }
        score = sf_add_score(score, bonus);
    }
    return score;
}

static SfScore sf_space_score(
    const Position *position,
    const SfEvalContext *context,
    Color color
) {
    uint64_t center = sf_center_files();
    uint64_t own_pawns = position->piece_occupied[
        color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN
    ];
    uint64_t mask = color == COLOR_WHITE
        ? center & sf_rows_mask(4, 6)
        : center & sf_rows_mask(1, 3);
    uint64_t safe = mask & ~own_pawns &
        ~context->attacks[opposite_color(color)][PIECE_TYPE_PAWN];
    uint64_t behind = own_pawns;
    uint64_t pawns = own_pawns;
    int step = color == COLOR_WHITE ? 1 : -1;
    int bonus;
    int weight;
    int non_pawn_material = 0;
    uint64_t pieces = position->occupied;
    SfScore score = {0, 0};

    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        Piece piece;

        pieces &= pieces - 1;
        piece = position_piece_at(position, square);
        if (piece_type(piece) != PIECE_TYPE_PAWN &&
            piece_type(piece) != PIECE_TYPE_KING) {
            non_pawn_material += eval_piece_type_value(piece_type(piece));
        }
    }
    if (non_pawn_material < 1222) {
        return score;
    }

    while (pawns != 0) {
        int square = __builtin_ctzll(pawns);
        int row = square_row(square);
        int column = square_column(square);
        int offset;

        pawns &= pawns - 1;
        for (offset = 1; offset <= 2; ++offset) {
            int behind_row = row + step * offset;

            if (is_valid_coordinate(behind_row, column)) {
                behind |= sf_bit(make_square(behind_row, column));
            }
        }
    }
    bonus = sf_popcount(safe) + sf_popcount(
        behind & safe & ~context->attacks[opposite_color(color)][SF_ALL_ATTACKS]
    );
    weight = sf_popcount(position->color_occupied[color]) - 1;
    score.mg = bonus * weight * weight / 16;
    return score;
}

static uint64_t sf_king_flank(int column) {
    int first_file;
    int last_file;
    uint64_t flank = 0;
    int row;
    int file;

    if (column <= 0) {
        first_file = 0;
        last_file = 2;
    } else if (column <= 2) {
        first_file = 0;
        last_file = 3;
    } else if (column <= 4) {
        first_file = 2;
        last_file = 5;
    } else if (column <= 6) {
        first_file = 4;
        last_file = 7;
    } else {
        first_file = 5;
        last_file = 7;
    }

    for (row = 0; row < BOARD_SIZE; ++row) {
        for (file = first_file; file <= last_file; ++file) {
            flank |= sf_bit(make_square(row, file));
        }
    }
    return flank;
}

static uint64_t sf_king_camp(Color color) {
    return color == COLOR_WHITE
        ? sf_rows_mask(3, 7)
        : sf_rows_mask(0, 4);
}

static SfScore sf_king_score(
    const Position *position,
    const SfEvalContext *context,
    Color color
) {
    Color enemy = opposite_color(color);
    int king_square = color == COLOR_WHITE
        ? position->white_king_square
        : position->black_king_square;
    int king_column;
    uint64_t flank;
    uint64_t camp;
    uint64_t weak;
    uint64_t safe;
    uint64_t unsafe_checks = 0;
    uint64_t rook_checks;
    uint64_t queen_checks;
    uint64_t bishop_checks;
    uint64_t knight_checks;
    uint64_t king_rays_orthogonal;
    uint64_t king_rays_diagonal;
    uint64_t enemy_attacks;
    uint64_t own_attacks;
    uint64_t own_queen;
    int flank_attack;
    int flank_defense;
    int king_danger;
    SfScore score = {0, 0};

    if (!is_valid_square(king_square)) {
        return score;
    }

    king_column = square_column(king_square);
    flank = sf_king_flank(king_column);
    camp = sf_king_camp(color);

    enemy_attacks = context->attacks[enemy][SF_ALL_ATTACKS];
    own_attacks = context->attacks[color][SF_ALL_ATTACKS];
    own_queen = position->piece_occupied[
        color == COLOR_WHITE ? PIECE_WHITE_QUEEN : PIECE_BLACK_QUEEN
    ];
    weak = enemy_attacks & ~context->attack2[color] &
        (~own_attacks | context->attacks[color][PIECE_TYPE_KING] |
            own_queen);

    /* Safe checking squares are a high-value part of Stockfish's classical
     * king term: they distinguish a real attacking position from a merely
     * crowded king ring. */
    safe = ~position->color_occupied[enemy] &
        (~own_attacks | (weak & context->attack2[enemy]));
    king_rays_orthogonal = sf_piece_attack_map_with_occupancy(
        king_square,
        PIECE_TYPE_ROOK,
        position->occupied & ~own_queen
    );
    king_rays_diagonal = sf_piece_attack_map_with_occupancy(
        king_square,
        PIECE_TYPE_BISHOP,
        position->occupied & ~own_queen
    );

    rook_checks = king_rays_orthogonal &
        context->attacks[enemy][PIECE_TYPE_ROOK] & safe;
    if (rook_checks != 0) {
        king_danger = sf_popcount(rook_checks) > 1 ? 967 : 645;
    } else {
        unsafe_checks |= king_rays_orthogonal &
            context->attacks[enemy][PIECE_TYPE_ROOK];
        king_danger = 0;
    }

    queen_checks = (king_rays_orthogonal | king_rays_diagonal) &
        context->attacks[enemy][PIECE_TYPE_QUEEN] & safe &
        ~(context->attacks[color][PIECE_TYPE_QUEEN] | rook_checks);
    if (queen_checks != 0) {
        king_danger += sf_popcount(queen_checks) > 1 ? 1897 : 1084;
    }

    bishop_checks = king_rays_diagonal &
        context->attacks[enemy][PIECE_TYPE_BISHOP] & safe &
        ~queen_checks;
    if (bishop_checks != 0) {
        king_danger += sf_popcount(bishop_checks) > 1 ? 967 : 645;
    } else {
        unsafe_checks |= king_rays_diagonal &
            context->attacks[enemy][PIECE_TYPE_BISHOP];
    }

    knight_checks = position_piece_attack_map(
        position,
        king_square,
        PIECE_TYPE_KNIGHT
    ) & context->attacks[enemy][PIECE_TYPE_KNIGHT];
    if ((knight_checks & safe) != 0) {
        king_danger += sf_popcount(knight_checks & safe) > 1
            ? 1119 : 772;
    } else {
        unsafe_checks |= knight_checks;
    }

    flank_attack = sf_popcount(enemy_attacks & flank & camp) +
        sf_popcount(context->attack2[enemy] & flank & camp);
    flank_defense = sf_popcount(own_attacks & flank & camp);
    king_danger +=
        148 * sf_popcount(unsafe_checks) +
        context->king_attackers_count[enemy] *
            context->king_attackers_weight[enemy] +
        185 * sf_popcount(context->king_ring[color] & weak) +
        98 * sf_popcount(position_pinned_pieces(position, color)) +
        69 * context->king_attacks_count[enemy] +
        3 * flank_attack * flank_attack / 8 +
        context->mobility_mg[enemy] -
        context->mobility_mg[color] -
        100 * ((context->attacks[color][PIECE_TYPE_KNIGHT] &
                context->attacks[color][PIECE_TYPE_KING]) != 0) -
        4 * flank_defense +
        37;
    if (position->piece_occupied[
            enemy == COLOR_WHITE ? PIECE_WHITE_QUEEN : PIECE_BLACK_QUEEN
        ] == 0) {
        king_danger -= 873;
    }
    if (king_danger > 100) {
        score.mg -= king_danger * king_danger / 4096;
        score.eg -= king_danger / 16;
    }
    if (((position->piece_occupied[PIECE_WHITE_PAWN] |
          position->piece_occupied[PIECE_BLACK_PAWN]) & flank) == 0) {
        score.mg -= 17;
        score.eg -= 95;
    }
    score.mg -= 8 * flank_attack;
    return score;
}

int stockfish_classical_score_with_breakdown(
    const Position *position,
    const struct ClassicalEvalState *state,
    StockfishClassicalBreakdown *breakdown
) {
    const EvalParams *params = current_eval_params();
    SfEvalContext context;
    SfScore score = {0, 0};
    SfScore component_scores[6] = {};
    int component_scales[6];
    int use_component_scales;
    int component_total = 0;
    int color;

    if (breakdown != 0) {
        *breakdown = (StockfishClassicalBreakdown){};
    }
    component_scales[0] = params->stockfish_piece_scale;
    component_scales[1] = params->stockfish_mobility_scale;
    component_scales[2] = params->stockfish_king_scale;
    component_scales[3] = params->stockfish_threat_scale;
    component_scales[4] = params->stockfish_passed_scale;
    component_scales[5] = params->stockfish_space_scale;
    use_component_scales = component_scales[0] != 0 ||
        component_scales[1] != 0 || component_scales[2] != 0 ||
        component_scales[3] != 0 || component_scales[4] != 0 ||
        component_scales[5] != 0;

    if (position == 0 || (!use_component_scales &&
                          params->stockfish_classical_scale == 0)) {
        return 0;
    }

    sf_build_context(position, state, &context);
    component_scores[0] = sf_material_imbalance(position);
    score = component_scores[0];
    for (color = COLOR_WHITE; color <= COLOR_BLACK; ++color) {
        int sign = color == COLOR_WHITE ? 1 : -1;
        SfScore side = {0, 0};

        side = sf_add_score(
            side,
            sf_piece_score(position, &context, (Color)color)
        );
        component_scores[0].mg += sign * side.mg;
        component_scores[0].eg += sign * side.eg;
        side = sf_add_score(side, sf_make_score(
            context.mobility_mg[color],
            context.mobility_eg[color]
        ));
        component_scores[1].mg += sign * context.mobility_mg[color];
        component_scores[1].eg += sign * context.mobility_eg[color];
        {
            SfScore component = sf_king_score(
                position,
                &context,
                (Color)color
            );

            side = sf_add_score(side, component);
            component_scores[2].mg += sign * component.mg;
            component_scores[2].eg += sign * component.eg;
        }
        {
            SfScore component = sf_threat_score(
                position,
                &context,
                (Color)color
            );

            side = sf_add_score(side, component);
            component_scores[3].mg += sign * component.mg;
            component_scores[3].eg += sign * component.eg;
        }
        {
            SfScore component = sf_passed_score(
                position,
                &context,
                (Color)color
            );

            side = sf_add_score(side, component);
            component_scores[4].mg += sign * component.mg;
            component_scores[4].eg += sign * component.eg;
        }
        {
            SfScore component = sf_space_score(
                position,
                &context,
                (Color)color
            );

            side = sf_add_score(side, component);
            component_scores[5].mg += sign * component.mg;
            component_scores[5].eg += sign * component.eg;
        }
        score.mg += sign * side.mg;
        score.eg += sign * side.eg;
    }

    {
        int *values[] = {
            breakdown != 0 ? &breakdown->piece : 0,
            breakdown != 0 ? &breakdown->mobility : 0,
            breakdown != 0 ? &breakdown->king : 0,
            breakdown != 0 ? &breakdown->threats : 0,
            breakdown != 0 ? &breakdown->passed : 0,
            breakdown != 0 ? &breakdown->space : 0
        };
        int index;

        for (index = 0; index < 6; ++index) {
            int raw = sf_taper_score(
                sf_scale_score(component_scores[index], SF_SCORE_SCALE),
                context.endgame_weight
            );
            int scaled = raw * (use_component_scales
                ? component_scales[index]
                : params->stockfish_classical_scale) / 256;

            component_total += scaled;
            if (values[index] != 0) {
                *values[index] = scaled;
            }
        }
        if (breakdown != 0) {
            breakdown->total = component_total;
        }
    }

    if (use_component_scales) {
        return component_total;
    }
    return sf_taper_score(
        sf_scale_score(score, SF_SCORE_SCALE),
        context.endgame_weight
    ) * params->stockfish_classical_scale / 256;
}

int stockfish_classical_score(
    const Position *position,
    const struct ClassicalEvalState *state
) {
    return stockfish_classical_score_with_breakdown(position, state, 0);
}
