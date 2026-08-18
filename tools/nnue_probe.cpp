#include <cstdio>

#include "src/chess/fen.h"
#include "src/eval/nnue.h"

int main(int argc, char **argv) {
    Position position;
    int score;

    if (argc != 3 || !position_from_fen(&position, argv[2]) ||
        !nnue_load(argv[1])) {
        std::fprintf(stderr, "usage: nnue_probe MODEL FEN\n");
        return 1;
    }

    nnue_set_enabled(1);
    if (!nnue_evaluate(&position, &score)) {
        return 1;
    }
    std::printf("%d\n", score);
    nnue_unload();
    return 0;
}
