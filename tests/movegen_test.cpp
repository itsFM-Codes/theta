#include <assert.h>
#include <stdint.h>

#include "src/chess/movegen.h"

static int count_moves_with_flag(const MoveList *moves, int flag) {
    int count = 0;
    int index;

    for (index = 0; index < moves->count; ++index) {
        if (moves->moves[index].flags & flag) {
            count++;
        }
    }

    return count;
}

static void assert_attack_map_matches(const Position *position) {
    Color color;

    for (color = COLOR_WHITE; color <= COLOR_BLACK;
         color = (Color)(color + 1)) {
        uint64_t attacks = position_attack_map(position, color);
        int square;

        for (square = 0; square < SQUARE_COUNT; ++square) {
            int mapped = (attacks & (UINT64_C(1) << square)) != 0;

            assert(mapped == is_square_attacked(position, square, color));
        }
    }
}

static uint32_t next_random(uint32_t *state) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static int old_move_gives_check(Position *position, Move move) {
    UndoState undo;
    int result;

    if (!make_move(position, move, &undo)) {
        return 0;
    }
    result = is_square_attacked(
        position,
        find_king(position, position->side_to_move),
        opposite_color(position->side_to_move)
    );
    undo_move(position, move, &undo);
    return result;
}

static void assert_move_check_helper_matches(void) {
    uint32_t random_state = UINT32_C(0x20260814);
    int game;

    for (game = 0; game < 80; ++game) {
        Position position;
        int ply;

        set_starting_position(&position);
        for (ply = 0; ply < 80; ++ply) {
            MoveList moves;
            int index;

            generate_moves(&position, &moves);
            for (index = 0; index < moves.count; ++index) {
                int expected = old_move_gives_check(
                    &position,
                    moves.moves[index]
                );
                int actual = position_move_gives_check(
                    &position,
                    moves.moves[index]
                );

                assert(expected == actual);
            }
            if (moves.count == 0) {
                break;
            }
            {
                Move move = moves.moves[
                    next_random(&random_state) % (uint32_t)moves.count
                ];
                UndoState undo;

                assert(make_move(&position, move, &undo));
            }
        }
    }
}

int main(void) {
    Position position;
    MoveList moves;

    /* A blocker on the up-right diagonal must stop a slider at the blocker. */
    clear_position(&position);
    position_set_piece_at_coordinates(
        &position,
        3,
        4,
        PIECE_WHITE_BISHOP
    );
    position_set_piece_at_coordinates(
        &position,
        2,
        5,
        PIECE_BLACK_PAWN
    );
    position_set_piece_at_coordinates(
        &position,
        1,
        6,
        PIECE_BLACK_PAWN
    );
    {
        uint64_t attacks = position_piece_attack_map(
            &position,
            make_square(3, 4),
            PIECE_TYPE_BISHOP
        );

        assert((attacks & (UINT64_C(1) << make_square(2, 5))) != 0);
        assert((attacks & (UINT64_C(1) << make_square(1, 6))) == 0);
    }

    set_starting_position(&position);
    generate_moves(&position, &moves);
    assert(moves.count == 20);
    assert(count_moves_with_flag(&moves, MOVE_FLAG_DOUBLE_PAWN) == 8);
    assert_attack_map_matches(&position);

    clear_position(&position);
    position_set_piece_at_coordinates(
        &position,
        4,
        3,
        PIECE_WHITE_KNIGHT
    );
    generate_moves(&position, &moves);
    assert(moves.count == 8);

    clear_position(&position);
    position_set_piece_at_coordinates(
        &position,
        1,
        3,
        PIECE_WHITE_PAWN
    );
    position_set_piece_at_coordinates(
        &position,
        0,
        2,
        PIECE_BLACK_ROOK
    );
    position_set_piece_at_coordinates(
        &position,
        0,
        4,
        PIECE_BLACK_ROOK
    );
    generate_moves(&position, &moves);
    assert(moves.count == 12);
    assert(count_moves_with_flag(&moves, MOVE_FLAG_PROMOTION) == 12);
    assert(count_moves_with_flag(&moves, MOVE_FLAG_CAPTURE) == 8);

    clear_position(&position);
    position.castling_rights =
        CASTLING_WHITE_KING_SIDE |
        CASTLING_WHITE_QUEEN_SIDE;
    position_set_piece_at_coordinates(
        &position,
        7,
        4,
        PIECE_WHITE_KING
    );
    position_set_piece_at_coordinates(
        &position,
        7,
        0,
        PIECE_WHITE_ROOK
    );
    position_set_piece_at_coordinates(
        &position,
        7,
        7,
        PIECE_WHITE_ROOK
    );
    generate_moves(&position, &moves);
    assert(count_moves_with_flag(&moves, MOVE_FLAG_CASTLE_KINGSIDE) == 1);
    assert(count_moves_with_flag(&moves, MOVE_FLAG_CASTLE_QUEENSIDE) == 1);
    assert_attack_map_matches(&position);

    assert_move_check_helper_matches();

    return 0;
}
