#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>

#include "src/chess/fen.h"
#include "src/chess/movegen.h"
#include "src/config/config.h"
#include "src/engine/search.h"
#include "src/eval/evaluation.h"
#include "src/uci/uci.h"

#define CONFIG_FILE "config/config.conf"
#define BENCH_DEPTH 5

typedef struct BenchSearchResult {
    SearchStatistics statistics;
} BenchSearchResult;

typedef struct TacticalCase {
    const char *fen;
    const char *expected_move;
} TacticalCase;

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

static void move_to_uci(Move move, char text[6]) {
    char promotion = promotion_character(move.promotion);

    if (!is_valid_square(move.from) || !is_valid_square(move.to)) {
        strcpy(text, "0000");
        return;
    }

    text[0] = (char)('a' + square_column(move.from));
    text[1] = (char)('8' - square_row(move.from));
    text[2] = (char)('a' + square_column(move.to));
    text[3] = (char)('8' - square_row(move.to));
    text[4] = promotion;
    text[promotion == '\0' ? 4 : 5] = '\0';
}

static void capture_bench_statistics(
    int depth,
    int score,
    const PrincipalVariation *variation,
    const SearchStatistics *statistics,
    void *user_data
) {
    BenchSearchResult *result = (BenchSearchResult *)user_data;

    (void)depth;
    (void)score;
    (void)variation;
    if (result != 0 && statistics != 0) {
        result->statistics = *statistics;
    }
}

static int run_benchmark(void) {
    static const char *positions[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/2pP4/1p2P3/2N2N2/PPQ1BPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP1P4/8/4k3/8/8 w - - 0 1",
        "4k3/8/8/3q4/8/8/4Q3/4K3 w - - 0 1"
    };
    uint64_t total_nodes = 0;
    int total_time_ms = 0;
    int index;

    printf("bench depth %d positions %d\n", BENCH_DEPTH,
           (int)(sizeof(positions) / sizeof(positions[0])));

    for (index = 0; index < (int)(sizeof(positions) / sizeof(positions[0]));
         ++index) {
        Position position;
        Move best_move;
        PrincipalVariation variation;
        BenchSearchResult result = {};
        int completed_depth;
        int score;
        char move[6];

        if (!position_from_fen(&position, positions[index])) {
            fprintf(stderr, "Error: Invalid benchmark position %d\n", index + 1);
            return 0;
        }

        score = search_iterative_with_callback(
            &position,
            BENCH_DEPTH,
            0,
            &best_move,
            &variation,
            &completed_depth,
            capture_bench_statistics,
            &result
        );
        move_to_uci(best_move, move);
        total_nodes += result.statistics.nodes;
        total_time_ms += result.statistics.elapsed_ms;

        printf("position %d depth %d score %d bestmove %s nodes ",
               index + 1, completed_depth, score, move);
        std::cout << result.statistics.nodes << '\n';
    }

    printf("total nodes ");
    std::cout << total_nodes << " time " << total_time_ms << " nps "
              << (total_time_ms > 0
                  ? total_nodes * 1000u / (uint64_t)total_time_ms
                  : 0)
              << '\n';
    return 1;
}

static int run_tactical_suite(void) {
    static const TacticalCase cases[] = {
        {
            "3r3k/8/8/8/8/8/8/K2Q4 w - - 0 1",
            "d1d8"
        },
        {
            "6k1/5ppp/8/8/8/8/6PP/3Q2K1 w - - 0 1",
            "d1d8"
        }
    };
    int passed = 0;
    char actual_moves[sizeof(cases) / sizeof(cases[0])][6];
    int results[sizeof(cases) / sizeof(cases[0])];
    int index;

    for (index = 0; index < (int)(sizeof(cases) / sizeof(cases[0])); ++index) {
        Position position;
        Move best_move;
        PrincipalVariation variation;
        int completed_depth;
        char move[6];
        int correct = 0;

        if (position_from_fen(&position, cases[index].fen)) {
            search_iterative(
                &position,
                2,
                0,
                &best_move,
                &variation,
                &completed_depth
            );
            move_to_uci(best_move, move);
            correct = strcmp(move, cases[index].expected_move) == 0;
        } else {
            strcpy(move, "0000");
        }

        if (correct) {
            passed++;
        }
        strcpy(actual_moves[index], move);
        results[index] = correct;
    }

    printf("{\"passed\":%d,\"total\":%d,\"cases\":[", passed,
           (int)(sizeof(cases) / sizeof(cases[0])));
    for (index = 0; index < (int)(sizeof(cases) / sizeof(cases[0])); ++index) {
        if (index > 0) {
            printf(",");
        }
        printf(
            "{\"id\":%d,\"expected\":\"%s\",\"actual\":\"%s\",\"passed\":%s}",
            index + 1,
            cases[index].expected_move,
            actual_moves[index],
            results[index] ? "true" : "false"
        );
    }
    printf("]}\n");
    return passed == (int)(sizeof(cases) / sizeof(cases[0]));
}

static int print_legal_moves(const char *fen) {
    Position position;
    MoveList moves;
    int king_square;
    int in_check;
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

    king_square = find_king(&position, position.side_to_move);
    in_check = is_valid_square(king_square) &&
        is_square_attacked(
            &position,
            king_square,
            opposite_color(position.side_to_move)
        );

    printf("{\"moves\":[");

    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];

        if (index > 0) {
            printf(",");
        }

        print_move_json(move);
    }

    printf(
        "],\"count\":%d,\"evaluation\":%d,\"inCheck\":%s}\n",
        moves.count,
        evaluation,
        in_check ? "true" : "false"
    );
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

    if (*text == '\0' || *end != '\0' || value < 1 ||
        value > g_config.max_depth) {
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

    if (argc == 1) {
        return run_uci() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc == 2 && strcmp(argv[1], "bench") == 0) {
        return run_benchmark() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc == 2 && strcmp(argv[1], "tactics") == 0) {
        return run_tactical_suite() ? EXIT_SUCCESS : EXIT_FAILURE;
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
