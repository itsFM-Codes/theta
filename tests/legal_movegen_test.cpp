#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "src/chess/fen.h"
#include "src/chess/movegen.h"

static uint64_t perft(Position *position, int depth) {
    MoveList moves;
    uint64_t nodes = 0;
    int index;

    if (depth == 0) {
        return 1;
    }

    generate_legal_moves(position, &moves);
    for (index = 0; index < moves.count; ++index) {
        UndoState undo;

        assert(make_move(position, moves.moves[index], &undo));
        nodes += perft(position, depth - 1);
        undo_move(position, moves.moves[index], &undo);
    }

    return nodes;
}

static int moves_are_equal(Move first, Move second) {
    return first.from == second.from &&
           first.to == second.to &&
           first.promotion == second.promotion &&
           first.flags == second.flags;
}

static void assert_bitboards_match_board(const Position *position) {
    uint64_t occupied = 0;
    uint64_t colors[COLOR_NONE] = {0, 0};
    uint64_t pieces[PIECE_BLACK_KING + 1] = {0};
    int square;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        Piece piece = position->board[square];
        if (piece != PIECE_NONE) {
            uint64_t mask = UINT64_C(1) << square;
            occupied |= mask;
            colors[piece_color(piece)] |= mask;
            pieces[piece] |= mask;
        }
    }

    assert(position->occupied == occupied);
    assert(position->color_occupied[COLOR_WHITE] == colors[COLOR_WHITE]);
    assert(position->color_occupied[COLOR_BLACK] == colors[COLOR_BLACK]);
    for (square = 0; square <= PIECE_BLACK_KING; ++square) {
        assert(position->piece_occupied[square] == pieces[square]);
    }
}

static void assert_fast_legal_filter(Position *position, int depth) {
    MoveList pseudo_moves;
    MoveList reference_moves;
    char before_fen[256];
    char after_fen[256];
    int index;

    if (depth <= 0) {
        return;
    }

    position_to_fen(position, before_fen, sizeof(before_fen));
    generate_moves(position, &pseudo_moves);
    assert_bitboards_match_board(position);
    generate_legal_moves(position, &reference_moves);
    assert_bitboards_match_board(position);
    position_to_fen(position, after_fen, sizeof(after_fen));
    assert(strcmp(before_fen, after_fen) == 0);
    for (index = 0; index < pseudo_moves.count; ++index) {
        Move move = pseudo_moves.moves[index];
        UndoState undo;
        int expected = 0;
        int reference_index;
        int actual;
        char fen[256];
        char move_before_fen[256];

        for (reference_index = 0;
             reference_index < reference_moves.count;
             ++reference_index) {
            if (moves_are_equal(move, reference_moves.moves[reference_index])) {
                expected = 1;
                break;
            }
        }

        position_to_fen(position, move_before_fen, sizeof(move_before_fen));
        actual = make_legal_move(position, move, &undo);
        if (!actual) {
            char move_after_fen[256];
            position_to_fen(position, move_after_fen, sizeof(move_after_fen));
            if (strcmp(move_before_fen, move_after_fen) != 0) {
                fprintf(
                    stderr,
                    "illegal move changed position move=%d-%d before=%s "
                    "after=%s\n",
                    move.from,
                    move.to,
                    move_before_fen,
                    move_after_fen
                );
            }
            assert(strcmp(move_before_fen, move_after_fen) == 0);
        }
        if (actual != expected) {
            position_to_fen(position, fen, sizeof(fen));
            fprintf(
                stderr,
                "fast legal mismatch depth=%d side=%d from=%d to=%d "
                "flags=%d promotion=%d piece=%d expected=%d actual=%d "
                "before=%s after=%s current=%s\n",
                depth,
                position->side_to_move,
                move.from,
                move.to,
                move.flags,
                move.promotion,
                position->board[move.from],
                expected,
                actual,
                before_fen,
                after_fen,
                fen
            );
        }
        assert(actual == expected);
        if (actual) {
            if (depth > 1) {
                assert_bitboards_match_board(position);
                assert_fast_legal_filter(position, depth - 1);
                assert_bitboards_match_board(position);
            }
            undo_move(position, move, &undo);
            assert_bitboards_match_board(position);
            {
                char restored_fen[256];
                position_to_fen(position, restored_fen, sizeof(restored_fen));
                assert(strcmp(restored_fen, before_fen) == 0);
            }
        }
    }
}

static uint64_t test_random_u64(uint64_t *state) {
    uint64_t value = *state;

    value ^= value << 7;
    value ^= value >> 9;
    value ^= value << 8;
    *state = value;
    return value;
}

static int test_positions_equal(const Position *first, const Position *second) {
    int square;
    int piece;

    for (square = 0; square < SQUARE_COUNT; ++square) {
        if (first->board[square] != second->board[square]) {
            return 0;
        }
    }
    if (first->occupied != second->occupied ||
        first->side_to_move != second->side_to_move ||
        first->castling_rights != second->castling_rights ||
        first->en_passant_square != second->en_passant_square ||
        first->halfmove_clock != second->halfmove_clock ||
        first->fullmove_number != second->fullmove_number ||
        first->white_king_square != second->white_king_square ||
        first->black_king_square != second->black_king_square) {
        return 0;
    }
    for (piece = 0; piece <= PIECE_BLACK_KING; ++piece) {
        if (first->piece_occupied[piece] != second->piece_occupied[piece]) {
            return 0;
        }
    }
    for (piece = COLOR_WHITE; piece < COLOR_NONE; ++piece) {
        if (first->color_occupied[piece] != second->color_occupied[piece]) {
            return 0;
        }
    }
    return 1;
}

static void assert_context_matches_reference(Position *position) {
    MoveList pseudo_moves;
    char before_fen[256];
    int king_square;
    int in_check;
    uint64_t pinned_pieces;
    int index;

    position_to_fen(position, before_fen, sizeof(before_fen));
    king_square = find_king(position, position->side_to_move);
    in_check = is_valid_square(king_square) &&
        is_square_attacked(position, king_square, opposite_color(position->side_to_move));
    pinned_pieces = position_pinned_pieces(position, position->side_to_move);
    generate_moves(position, &pseudo_moves);

    for (index = 0; index < pseudo_moves.count; ++index) {
        Move move = pseudo_moves.moves[index];
        Position reference = *position;
        Position optimized = *position;
        UndoState reference_undo;
        UndoState optimized_undo;
        char reference_fen[256];
        char optimized_fen[256];
        int reference_result = make_legal_move(
            &reference,
            move,
            &reference_undo
        );
        int optimized_result = make_legal_move_with_context(
            &optimized,
            move,
            &optimized_undo,
            in_check,
            pinned_pieces
        );

        position_to_fen(&reference, reference_fen, sizeof(reference_fen));
        position_to_fen(&optimized, optimized_fen, sizeof(optimized_fen));
        if (reference_result != optimized_result ||
            !test_positions_equal(&reference, &optimized)) {
            fprintf(
                stderr,
                "context move mismatch before=%s move=%d-%d flags=%d "
                "reference=%d optimized=%d reference_after=%s "
                "optimized_after=%s\n",
                before_fen,
                move.from,
                move.to,
                move.flags,
                reference_result,
                optimized_result,
                reference_fen,
                optimized_fen
            );
        }
        assert(reference_result == optimized_result);
        assert(test_positions_equal(&reference, &optimized));

        if (reference_result) {
            undo_move(&reference, move, &reference_undo);
        }
        if (optimized_result) {
            undo_move(&optimized, move, &optimized_undo);
        }
        assert(test_positions_equal(&reference, position));
        assert(test_positions_equal(&optimized, position));
    }
}

static void test_context_random_positions(void) {
    uint64_t random_state = UINT64_C(0x9e3779b97f4a7c15);
    int game;

    for (game = 0; game < 12; ++game) {
        Position position;
        int ply;

        set_starting_position(&position);
        for (ply = 0; ply < 35; ++ply) {
            MoveList legal_moves;

            assert_context_matches_reference(&position);
            generate_legal_moves(&position, &legal_moves);
            if (legal_moves.count == 0) {
                break;
            }
            {
                Move move = legal_moves.moves[
                    test_random_u64(&random_state) % legal_moves.count
                ];
                UndoState undo;

                assert(make_move(&position, move, &undo));
            }
        }
    }
}

static void test_starting_position(void) {
    Position position;

    set_starting_position(&position);
    assert(perft(&position, 1) == 20);
    assert(perft(&position, 2) == 400);
    assert(perft(&position, 3) == 8902);
    assert(perft(&position, 4) == 197281);
    assert(perft(&position, 5) == 4865609);
}

static void test_kiwipete_position(void) {
    Position position;
    const char *fen =
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R "
        "w KQkq - 0 1";

    assert(position_from_fen(&position, fen));
    assert(perft(&position, 1) == 48);
    assert(perft(&position, 2) == 2039);
    assert(perft(&position, 3) == 97862);
    assert(perft(&position, 4) == 4085603);
}

static void test_promotion_and_castling_position(void) {
    Position position;
    const char *fen =
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 "
        "w kq - 0 1";

    assert(position_from_fen(&position, fen));
    assert(perft(&position, 1) == 6);
    assert(perft(&position, 2) == 264);
    assert(perft(&position, 3) == 9467);
    assert(perft(&position, 4) == 422333);
}

static void test_en_passant_and_rook_endgame_position(void) {
    Position position;
    const char *fen =
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";

    assert(position_from_fen(&position, fen));
    assert(perft(&position, 1) == 14);
    assert(perft(&position, 2) == 191);
    assert(perft(&position, 3) == 2812);
    assert(perft(&position, 4) == 43238);
    assert(perft(&position, 5) == 674624);
}

static void test_discovered_check_and_castling_position(void) {
    Position position;
    const char *fen =
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R "
        "w KQ - 1 8";

    assert(position_from_fen(&position, fen));
    assert(perft(&position, 1) == 44);
    assert(perft(&position, 2) == 1486);
    assert(perft(&position, 3) == 62379);
    assert(perft(&position, 4) == 2103487);
}

static void test_pinned_en_passant_is_illegal(void) {
    Position position;

    assert(position_from_fen(
        &position,
        "k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"
    ));
    assert(perft(&position, 1) == 6);
    assert(perft(&position, 2) == 77);
}

static void test_tactical_middlegame_position(void) {
    Position position;

    assert(position_from_fen(
        &position,
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/"
        "P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"
    ));
    assert(perft(&position, 1) == 46);
    assert(perft(&position, 2) == 2079);
    assert(perft(&position, 3) == 89890);
    assert(perft(&position, 4) == 3894594);
}

static void test_castling_and_promotion_stress_position(void) {
    Position position;

    assert(position_from_fen(
        &position,
        "r7/4p3/5p1q/3P4/4pQ2/4pP2/6pp/R3K1kr "
        "w Q - 1 3"
    ));
    assert(perft(&position, 1) == 29);
    assert(perft(&position, 2) == 681);
    assert(perft(&position, 3) == 18511);
    assert(perft(&position, 4) == 430036);
}

int main(void) {
    test_starting_position();
    test_kiwipete_position();
    test_promotion_and_castling_position();
    test_en_passant_and_rook_endgame_position();
    test_discovered_check_and_castling_position();
    test_pinned_en_passant_is_illegal();
    test_tactical_middlegame_position();
    test_castling_and_promotion_stress_position();

    {
        Position position;

        set_starting_position(&position);
        assert_fast_legal_filter(&position, 3);
        assert(position_from_fen(
            &position,
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/"
            "PPPBBPPP/R3K2R w KQkq - 0 1"
        ));
        assert_fast_legal_filter(&position, 2);
        assert(position_from_fen(
            &position,
            "k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"
        ));
        assert_fast_legal_filter(&position, 2);
    }
    test_context_random_positions();
    return 0;
}
