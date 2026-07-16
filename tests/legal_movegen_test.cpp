#include <assert.h>
#include <stdint.h>

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

int main(void) {
    Position position;

    set_starting_position(&position);
    assert(perft(&position, 1) == 20);
    assert(perft(&position, 2) == 400);
    assert(perft(&position, 3) == 8902);
    assert(perft(&position, 4) == 197281);
    return 0;
}
