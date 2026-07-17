#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/chess/fen.h"
#include "src/chess/movegen.h"
#include "src/config/config.h"
#include "src/engine/search.h"
#include "src/eval/evaluation.h"

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

static void print_move_json(Move move) {
    char from[3];
    char to[3];
    char promotion = promotion_character(move.promotion);

    square_name(move.from, from);
    square_name(move.to, to);

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

static int print_legal_moves(const char *fen) {
    Position position;
    MoveList moves;
    int evaluation;
    int index;

    if (!position_from_fen(&position, fen)) {
        fprintf(stderr, "Error: Invalid FEN\n");
        return 0;
    }

    generate_legal_moves(&position, &moves);
    evaluation = evaluate_position(&position);

    if (position.side_to_move == COLOR_BLACK) {
        evaluation = -evaluation;
    }

    printf("{\"moves\":[");

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];

        if (index > 0) {
            printf(",");
        }

        print_move_json(move);
    }

    printf("],\"count\":%d,\"evaluation\":%d}\n", moves.count, evaluation);
    return 1;
}

static int print_search_result(const char *fen, int depth, int time_limit_ms) {
    Position position;
    Move best_move;
    PrincipalVariation variation;
    int completed_depth;
    int evaluation;
    int index;

    if (!position_from_fen(&position, fen)) {
        fprintf(stderr, "Error: Invalid FEN\n");
        return 0;
    }

    evaluation = search_iterative(
        &position,
        depth,
        time_limit_ms,
        &best_move,
        &variation,
        &completed_depth
    );

    if (position.side_to_move == COLOR_BLACK) {
        evaluation = -evaluation;
    }

    printf(
        "{\"evaluation\":%d,\"depth\":%d,\"pv\":[",
        evaluation,
        completed_depth
    );

    for (index = 0; index < variation.count; ++index) {
        if (index > 0) {
            printf(",");
        }

        print_move_json(variation.moves[index]);
    }

    printf("]}\n");
    return 1;
}

static int parse_depth(const char *text, int *depth) {
    char *end;
    long value;

    if (text == 0 || depth == 0) {
        return 0;
    }

    value = strtol(text, &end, 10);

    if (*text == '\0' || *end != '\0' || value < 1 || value > 8) {
        return 0;
    }

    *depth = (int)value;
    return 1;
}

static int parse_time_limit(const char *text, int *time_limit_ms) {
    char *end;
    long value;

    if (text == 0 || time_limit_ms == 0) {
        return 0;
    }

    value = strtol(text, &end, 10);

    if (*text == '\0' || *end != '\0' || value < 0 || value > 60000) {
        return 0;
    }

    *time_limit_ms = (int)value;
    return 1;
}

int main(int argc, char **argv) {
    int depth;
    int time_limit_ms;

    if (!load_config(CONFIG_FILE)) {
        return EXIT_FAILURE;
    }

    if (argc == 3 && strcmp(argv[1], "moves") == 0) {
        return print_legal_moves(argv[2]) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc == 4 && strcmp(argv[1], "search") == 0 &&
        parse_depth(argv[2], &depth)) {
        return print_search_result(argv[3], depth, 0)
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
    }

    if (argc == 5 && strcmp(argv[1], "search") == 0 &&
        parse_depth(argv[2], &depth) &&
        parse_time_limit(argv[3], &time_limit_ms)) {
        return print_search_result(argv[4], depth, time_limit_ms)
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
