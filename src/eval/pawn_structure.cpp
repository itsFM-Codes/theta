#include "pawn_structure.h"

#include <stdint.h>

#define DOUBLED_PAWN_PENALTY 12
#define ISOLATED_PAWN_PENALTY 10
#define PAWN_ISLAND_PENALTY 6
#define BACKWARD_PAWN_PENALTY 8
#define CANDIDATE_PASSED_PAWN_BONUS 6
#define SUPPORTED_PASSED_PAWN_BONUS 6
#define CONNECTED_PASSED_PAWN_BONUS 5
#define BLOCKED_PASSED_PAWN_PENALTY 8
#define PASSED_PAWN_KING_DISTANCE_SCALE 2
#define PAWN_HASH_SIZE (1 << 14)

typedef struct PawnHashEntry {
    uint64_t key;
    int score;
    int is_valid;
} PawnHashEntry;

static thread_local PawnHashEntry pawn_hash[PAWN_HASH_SIZE];

static uint64_t pawn_structure_key(const Position *position) {
    uint64_t key = UINT64_C(1469598103934665603);
    int square;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        Piece piece = position_piece_at(position, square);
        PieceType type = piece_type(piece);

        if (type == PIECE_TYPE_PAWN || type == PIECE_TYPE_KING) {
            key ^= (uint64_t)(piece + 1) * 67u + (uint64_t)square;
            key *= UINT64_C(1099511628211);
        } else if (type != PIECE_TYPE_NONE) {
            // Only occupancy affects pawn blockage.
            key ^= UINT64_C(4099) + (uint64_t)square;
            key *= UINT64_C(1099511628211);
        }
    }
    return key;
}

static int count_pawns_on_file(
    const Position *position,
    Color color,
    int column
) {
    int count = 0;
    int row;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;

    for (row = 0; row < BOARD_SIZE; ++row) {
        if (position_piece_at_coordinates(position, row, column) == pawn) {
            ++count;
        }
    }

    return count;
}

static int pawn_is_passed(const Position *position, int square, Color color) {
    int row = square_row(square);
    int column = square_column(square);
    int row_step = color == COLOR_WHITE ? -1 : 1;
    Piece opposing_pawn = color == COLOR_WHITE
        ? PIECE_BLACK_PAWN
        : PIECE_WHITE_PAWN;

    row += row_step;

    while (row >= 0 && row < BOARD_SIZE) {
        int file;

        for (file = column - 1; file <= column + 1; ++file) {
            if (file >= 0 && file < BOARD_SIZE &&
                position_piece_at_coordinates(position, row, file) == opposing_pawn) {
                return 0;
            }
        }

        row += row_step;
    }

    return 1;
}

static int passed_pawn_bonus(int square, Color color) {
    int row = square_row(square);
    int advancement = color == COLOR_WHITE ? 6 - row : row - 1;

    return 10 + advancement * 10;
}

static int king_square(const Position *position, Color color) {
    Piece king = color == COLOR_WHITE ? PIECE_WHITE_KING : PIECE_BLACK_KING;
    int square;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        if (position_piece_at(position, square) == king) {
            return square;
        }
    }

    return NO_SQUARE;
}

static int distance_between_squares(int first, int second) {
    int row_distance;
    int column_distance;

    if (!is_valid_square(first) || !is_valid_square(second)) {
        return 0;
    }

    row_distance = square_row(first) - square_row(second);
    column_distance = square_column(first) - square_column(second);
    if (row_distance < 0) {
        row_distance = -row_distance;
    }
    if (column_distance < 0) {
        column_distance = -column_distance;
    }

    return row_distance > column_distance ? row_distance : column_distance;
}

static int passed_pawn_king_distance_bonus(
    const Position *position,
    int square,
    Color color
) {
    int own_king = king_square(position, color);
    int opposing_king = king_square(position, opposite_color(color));
    int promotion_square = make_square(
        color == COLOR_WHITE ? 0 : BOARD_SIZE - 1,
        square_column(square)
    );
    int own_distance;
    int opposing_distance;
    int bonus;

    if (!is_valid_square(own_king) || !is_valid_square(opposing_king)) {
        return 0;
    }

    own_distance = distance_between_squares(own_king, square);
    opposing_distance = distance_between_squares(opposing_king, promotion_square);
    bonus = (opposing_distance - own_distance) *
            PASSED_PAWN_KING_DISTANCE_SCALE;

    if (bonus > 16) {
        return 16;
    }
    if (bonus < -16) {
        return -16;
    }

    return bonus;
}

static int pawn_is_supported(const Position *position, int square, Color color) {
    int row = square_row(square);
    int column = square_column(square);
    int supporter_row = color == COLOR_WHITE ? row + 1 : row - 1;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int file;

    if (supporter_row < 0 || supporter_row >= BOARD_SIZE) {
        return 0;
    }

    for (file = column - 1; file <= column + 1; file += 2) {
        if (file >= 0 && file < BOARD_SIZE &&
            position_piece_at_coordinates(position, supporter_row, file) == pawn) {
            return 1;
        }
    }

    return 0;
}

static int pawn_is_connected_passed(
    const Position *position,
    int square,
    Color color
) {
    int row = square_row(square);
    int column = square_column(square);
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int file;
    int connected_row;

    for (file = column - 1; file <= column + 1; file += 2) {
        if (file < 0 || file >= BOARD_SIZE) {
            continue;
        }

        for (connected_row = row - 1;
             connected_row <= row + 1;
             ++connected_row) {
            int connected_square;

            if (connected_row < 0 || connected_row >= BOARD_SIZE) {
                continue;
            }

            connected_square = make_square(connected_row, file);
            if (position_piece_at(position, connected_square) == pawn &&
                pawn_is_passed(position, connected_square, color)) {
                return 1;
            }
        }
    }

    return 0;
}

static int pawn_is_blocked(const Position *position, int square, Color color) {
    int row = square_row(square);
    int column = square_column(square);
    int front_row = color == COLOR_WHITE ? row - 1 : row + 1;

    return front_row >= 0 && front_row < BOARD_SIZE &&
           position_piece_at_coordinates(position, front_row, column) != PIECE_NONE;
}

static int pawn_controls_square(
    const Position *position,
    Color color,
    int target_square
) {
    int target_row = square_row(target_square);
    int target_column = square_column(target_square);
    int pawn_row = color == COLOR_WHITE ? target_row + 1 : target_row - 1;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int file;

    if (pawn_row < 0 || pawn_row >= BOARD_SIZE) {
        return 0;
    }

    for (file = target_column - 1; file <= target_column + 1; file += 2) {
        if (file >= 0 && file < BOARD_SIZE &&
            position_piece_at_coordinates(position, pawn_row, file) == pawn) {
            return 1;
        }
    }

    return 0;
}

static int pawn_has_adjacent_helper(
    const Position *position,
    int square,
    Color color
) {
    int row = square_row(square);
    int column = square_column(square);
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int file;

    for (file = column - 1; file <= column + 1; file += 2) {
        int helper_row;

        if (file < 0 || file >= BOARD_SIZE) {
            continue;
        }

        for (helper_row = 0; helper_row < BOARD_SIZE; ++helper_row) {
            if (position_piece_at_coordinates(position, helper_row, file) !=
                pawn) {
                continue;
            }

            if ((color == COLOR_WHITE && helper_row >= row) ||
                (color == COLOR_BLACK && helper_row <= row)) {
                return 1;
            }
        }
    }

    return 0;
}

static int count_opposing_pawns_ahead(
    const Position *position,
    int square,
    Color color,
    int include_adjacent_files
) {
    int row = square_row(square);
    int column = square_column(square);
    int row_step = color == COLOR_WHITE ? -1 : 1;
    Piece opposing_pawn = color == COLOR_WHITE
        ? PIECE_BLACK_PAWN
        : PIECE_WHITE_PAWN;
    int count = 0;

    row += row_step;
    while (row >= 0 && row < BOARD_SIZE) {
        int file_start = include_adjacent_files ? column - 1 : column;
        int file_end = include_adjacent_files ? column + 1 : column;
        int file;

        for (file = file_start; file <= file_end; ++file) {
            if (file >= 0 && file < BOARD_SIZE &&
                position_piece_at_coordinates(position, row, file) ==
                    opposing_pawn) {
                count++;
            }
        }

        row += row_step;
    }

    return count;
}

static int pawn_is_backward(const Position *position, int square, Color color) {
    int row = square_row(square);
    int column = square_column(square);
    int front_row = color == COLOR_WHITE ? row - 1 : row + 1;
    int front_square;

    if (pawn_is_passed(position, square, color) ||
        pawn_has_adjacent_helper(position, square, color) ||
        front_row < 0 || front_row >= BOARD_SIZE) {
        return 0;
    }

    front_square = make_square(front_row, column);
    return pawn_controls_square(position, opposite_color(color), front_square);
}

static int pawn_is_candidate_passed(
    const Position *position,
    int square,
    Color color
) {
    if (pawn_is_passed(position, square, color) ||
        pawn_is_backward(position, square, color)) {
        return 0;
    }

    return count_opposing_pawns_ahead(position, square, color, 0) == 0 &&
           count_opposing_pawns_ahead(position, square, color, 1) <= 1 &&
           pawn_has_adjacent_helper(position, square, color);
}

static int pawn_island_count(const Position *position, Color color) {
    int islands = 0;
    int in_island = 0;
    int column;

    for (column = 0; column < BOARD_SIZE; ++column) {
        if (count_pawns_on_file(position, color, column) > 0) {
            if (!in_island) {
                islands++;
                in_island = 1;
            }
        } else {
            in_island = 0;
        }
    }

    return islands;
}

static int side_pawn_structure_score(const Position *position, Color color) {
    int score = 0;
    int column;
    int square;
    Piece pawn = color == COLOR_WHITE ? PIECE_WHITE_PAWN : PIECE_BLACK_PAWN;
    int islands = pawn_island_count(position, color);

    if (islands > 1) {
        score -= (islands - 1) * PAWN_ISLAND_PENALTY;
    }

    for (column = 0; column < BOARD_SIZE; ++column) {
        int count = count_pawns_on_file(position, color, column);

        if (count > 1) {
            score -= (count - 1) * DOUBLED_PAWN_PENALTY;
        }
    }

    for (square = 0; square < SQUARE_COUNT; ++square) {
        int column;
        int has_adjacent_pawn = 0;

        if (position_piece_at(position, square) != pawn) {
            continue;
        }

        column = square_column(square);

        if (column > 0 && count_pawns_on_file(position, color, column - 1) > 0) {
            has_adjacent_pawn = 1;
        }

        if (column < BOARD_SIZE - 1 &&
            count_pawns_on_file(position, color, column + 1) > 0) {
            has_adjacent_pawn = 1;
        }

        if (!has_adjacent_pawn) {
            score -= ISOLATED_PAWN_PENALTY;
        }

        if (pawn_is_passed(position, square, color)) {
            int advancement = color == COLOR_WHITE
                ? 6 - square_row(square)
                : square_row(square) - 1;

            score += passed_pawn_bonus(square, color);
            score += passed_pawn_king_distance_bonus(position, square, color);
            if (pawn_is_supported(position, square, color)) {
                score += SUPPORTED_PASSED_PAWN_BONUS + advancement * 2;
            }
            if (pawn_is_connected_passed(position, square, color)) {
                score += CONNECTED_PASSED_PAWN_BONUS + advancement * 2;
            }
            if (pawn_is_blocked(position, square, color)) {
                score -= BLOCKED_PASSED_PAWN_PENALTY + advancement * 2;
            }
        } else if (pawn_is_candidate_passed(position, square, color)) {
            int advancement = color == COLOR_WHITE
                ? 6 - square_row(square)
                : square_row(square) - 1;

            score += CANDIDATE_PASSED_PAWN_BONUS + advancement * 3;
        }

        if (pawn_is_backward(position, square, color)) {
            score -= BACKWARD_PAWN_PENALTY;
        }
    }

    return score;
}

int pawn_structure_score(const Position *position) {
    uint64_t key;
    PawnHashEntry *entry;
    int score;

    if (position == 0) {
        return 0;
    }

    key = pawn_structure_key(position);
    entry = &pawn_hash[key % PAWN_HASH_SIZE];
    if (entry->is_valid && entry->key == key) {
        return entry->score;
    }

    score = side_pawn_structure_score(position, COLOR_WHITE) -
            side_pawn_structure_score(position, COLOR_BLACK);
    entry->key = key;
    entry->score = score;
    entry->is_valid = 1;
    return score;
}
