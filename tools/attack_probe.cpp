#include <stdio.h>

#include "src/chess/fen.h"
#include "src/chess/movegen.h"

int main(int argc, char **argv) {
    Position position;
    MoveList moves;
    if (argc != 2 || !position_from_fen(&position, argv[1])) {
        return 1;
    }
    generate_moves(&position, &moves);
    printf("attacks=%016llx moves=%d\n",
           (unsigned long long)position_piece_attack_map(
               &position,
               make_square(4, 4),
               PIECE_TYPE_BISHOP
           ),
           moves.count);
    for (int index = 0; index < moves.count; ++index) {
        printf("%d-%d ", moves.moves[index].from, moves.moves[index].to);
    }
    printf("\n");
    return 0;
}
