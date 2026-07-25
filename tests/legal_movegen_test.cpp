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

int main(void) {
    test_starting_position();
    test_kiwipete_position();
    test_promotion_and_castling_position();
    return 0;
}
