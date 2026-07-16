#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/chess/fen.h"
#include "src/chess/movegen.h"
#include "src/config/config.h"

#define CONFIG_FILE "config/config.conf"

static char promotion_character(Piece piece) {
    switch (piece_type(piece)) {
        case PIECE_TYPE_QUEEN:
            return 'q';
        case PIECE_TYPE_ROOK:
            return 'r';
        case PIECE_TYPE_BISHOP:
            return 'b';
        case PIECE_TYPE_KNIGHT:
            return 'n';
        default:
            return '\0';
    }
}

static void square_name(int square, char name[3]) {
    name[0] = (char)('a' + square_column(square));
    name[1] = (char)('8' - square_row(square));
    name[2] = '\0';
}

static int print_legal_moves(const char *fen) {
    Position position;
    MoveList moves;
    int index;

    if (!position_from_fen(&position, fen)) {
        fprintf(stderr, "Error: Invalid FEN\n");
        return 0;
    }

    generate_legal_moves(&position, &moves);
    printf("{\"moves\":[");

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        char from[3];
        char to[3];
        char promotion = promotion_character(move.promotion);

        square_name(move.from, from);
        square_name(move.to, to);

        if (index > 0) {
            printf(",");
        }

        printf(
            "{\"from\":\"%s\",\"to\":\"%s\",\"flags\":%d,",
            from,
            to,
            move.flags
        );

        if (promotion != '\0') {
            printf("\"promotion\":\"%c\"}", promotion);
        } else {
            printf("\"promotion\":null}");
        }
    }

    printf("],\"count\":%d}\n", moves.count);
    return 1;
}

int main(int argc, char **argv) {
    if (!load_config(CONFIG_FILE)) {
        return EXIT_FAILURE;
    }

    if (argc == 3 && strcmp(argv[1], "moves") == 0) {
        return print_legal_moves(argv[2]) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
