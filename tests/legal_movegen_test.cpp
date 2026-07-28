#include <assert.h>
#include <stdint.h>

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

int main(void) {
    test_starting_position();
    test_kiwipete_position();
    test_promotion_and_castling_position();
    test_en_passant_and_rook_endgame_position();
    test_discovered_check_and_castling_position();
    return 0;
}
