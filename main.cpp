#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "src/chess/fen.h"
#include "src/chess/movegen.h"
#include "src/config/config.h"
#include "src/engine/search.h"
#include "src/engine/search_internal.h"
#include "src/eval/evaluation.h"
#include "src/eval/eval_tuning.h"
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
    uint64_t total_raw_evaluations = 0;
    uint64_t total_static_evaluation_calls = 0;
    uint64_t total_static_evaluation_cache_hits = 0;
    uint64_t total_see_calls = 0;
    uint64_t total_zobrist_rebuilds = 0;
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
        total_raw_evaluations += result.statistics.raw_evaluations;
        total_static_evaluation_calls +=
            result.statistics.static_evaluation_calls;
        total_static_evaluation_cache_hits +=
            result.statistics.static_evaluation_cache_hits;
        total_see_calls += result.statistics.see_calls;
        total_zobrist_rebuilds += result.statistics.zobrist_rebuilds;
        total_time_ms += result.statistics.elapsed_ms;

        printf("position %d depth %d score %d bestmove %s nodes ",
               index + 1, completed_depth, score, move);
        std::cout << result.statistics.nodes
                  << " raweval " << result.statistics.raw_evaluations
                  << " see " << result.statistics.see_calls
                  << " keyrebuild " << result.statistics.zobrist_rebuilds
                  << '\n';
    }

    printf("total nodes ");
    std::cout << total_nodes << " time " << total_time_ms << " nps "
              << (total_time_ms > 0
                  ? total_nodes * 1000u / (uint64_t)total_time_ms
                  : 0)
              << " raweval " << total_raw_evaluations
              << " staticeval " << total_static_evaluation_calls
              << " statichit " << total_static_evaluation_cache_hits
              << " see " << total_see_calls
              << " keyrebuild " << total_zobrist_rebuilds
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

static int print_evaluation_trace(const char *fen) {
    Position position;
    EvaluationTrace trace = {};

    if (!position_from_fen(&position, fen)) {
        fprintf(stderr, "Error: Invalid FEN\n");
        return 0;
    }

    evaluate_position_with_trace(&position, &trace);
    printf(
        "{\"materialPst\":%d,\"mobility\":%d,\"pawnStructure\":%d,"
        "\"kingSafety\":%d,\"pieceActivity\":%d,\"threats\":%d,"
        "\"space\":%d,\"tempo\":%d,\"advanced\":%d,"
        "\"stockfish\":%d,\"stockfishPiece\":%d,"
        "\"stockfishMobility\":%d,\"stockfishKing\":%d,"
        "\"stockfishThreats\":%d,\"stockfishPassed\":%d,"
        "\"stockfishSpace\":%d,"
        "\"total\":%d}\n",
        trace.material_and_piece_square,
        trace.mobility,
        trace.pawn_structure,
        trace.king_safety,
        trace.piece_activity,
        trace.threats,
        trace.space,
        trace.tempo,
        trace.advanced,
        trace.stockfish_classical,
        trace.stockfish_piece,
        trace.stockfish_mobility,
        trace.stockfish_king,
        trace.stockfish_threats,
        trace.stockfish_passed,
        trace.stockfish_space,
        trace.total
    );
    return 1;
}

static uint64_t next_random_value(uint64_t *seed) {
    uint64_t value;

    if (seed == 0) {
        return 0;
    }

    value = *seed;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *seed = value;
    return value;
}

static int game_result(
    Position *position,
    int ply,
    int max_plies,
    double *result
) {
    MoveList moves;

    if (position == 0 || result == 0) {
        return 1;
    }

    generate_legal_moves(position, &moves);
    if (moves.count == 0) {
        if (position_is_in_check(position)) {
            *result = position->side_to_move == COLOR_WHITE ? 0.0 : 1.0;
        } else {
            *result = 0.5;
        }
        return 1;
    }

    if (position->halfmove_clock >= 100 ||
        position_has_insufficient_material(position) ||
        ply >= max_plies) {
        *result = 0.5;
        return 1;
    }

    return 0;
}

static int score_selfplay_move(
    Position *position,
    Move move,
    int depth,
    int *score
) {
    SearchSharedState shared_state;
    UndoState undo;
    Move ignored_move;
    int child_score;

    if (position == 0 || score == 0 ||
        !make_move(position, move, &undo)) {
        return 0;
    }

    if (depth <= 0) {
        child_score = evaluate_position(position);
    } else {
        if (!initialize_search_shared_state(&shared_state)) {
            undo_move(position, move, &undo);
            return 0;
        }
        child_score = search_position_with_state(
            &shared_state,
            position,
            depth,
            &ignored_move
        );
        destroy_search_shared_state(&shared_state);
    }

    undo_move(position, move, &undo);
    *score = -child_score;
    return 1;
}

static int choose_selfplay_move(
    Position *position,
    int depth,
    int ply,
    uint64_t *seed,
    Move *chosen_move
) {
    MoveList moves;
    int scores[MAX_MOVES];
    int selected[MAX_MOVES];
    int selected_count = 0;
    int best_score = -SEARCH_INFINITY;
    int margin = ply < 12 ? 120 : 60;
    int index;

    if (position == 0 || chosen_move == 0) {
        return 0;
    }

    generate_legal_moves(position, &moves);
    if (moves.count <= 0) {
        return 0;
    }

    for (index = 0; index < moves.count; ++index) {
        if (!score_selfplay_move(
                position,
                moves.moves[index],
                depth - 1,
                &scores[index]
            )) {
            return 0;
        }
        if (scores[index] > best_score) {
            best_score = scores[index];
        }
    }

    for (index = 0; index < moves.count; ++index) {
        if (scores[index] >= best_score - margin) {
            selected[selected_count++] = index;
        }
    }

    if (selected_count == 0) {
        return 0;
    }

    *chosen_move = moves.moves[
        selected[next_random_value(seed) % (uint64_t)selected_count]
    ];
    return 1;
}

static int run_selfplay_dataset(
    int games,
    int depth,
    int max_plies,
    const char *output_path
) {
    std::ofstream output(output_path);
    int game;
    int written = 0;

    if (!output.is_open()) {
        fprintf(stderr, "Error: Could not write %s\n", output_path);
        return 0;
    }

    for (game = 0; game < games; ++game) {
        Position position;
        std::vector<std::string> positions;
        uint64_t seed = UINT64_C(0x9e3779b97f4a7c15) ^
                        (uint64_t)(game + 1);
        double result = 0.5;
        int ply;

        set_starting_position(&position);

        for (ply = 0; ply < max_plies; ++ply) {
            char fen[128];
            Move move;
            UndoState undo;

            if (game_result(&position, ply, max_plies, &result)) {
                break;
            }
            if (!position_to_fen(&position, fen, sizeof(fen))) {
                fprintf(stderr, "Error: Could not write FEN\n");
                return 0;
            }
            positions.push_back(fen);
            if (!choose_selfplay_move(&position, depth, ply, &seed, &move) ||
                !make_move(&position, move, &undo)) {
                fprintf(stderr, "Error: Self play move failed\n");
                return 0;
            }
        }

        game_result(&position, max_plies, max_plies, &result);
        for (size_t index = 0; index < positions.size(); ++index) {
            output << result << " " << positions[index] << "\n";
            written++;
        }

        std::cout << "selfplay game " << (game + 1)
                  << " positions " << positions.size()
                  << " result " << result << "\n";
    }

    std::cout << "selfplay dataset " << output_path
              << " rows " << written << "\n";
    return 1;
}

static int run_random_dataset(
    int games,
    int max_plies,
    const char *output_path
) {
    std::ofstream output(output_path);
    int game;

    if (!output.is_open()) {
        fprintf(stderr, "Error: Could not write %s\n", output_path);
        return 0;
    }

    for (game = 0; game < games; ++game) {
        Position position;
        uint64_t seed = UINT64_C(0x9e3779b97f4a7c15) ^
                        (uint64_t)(game + 1) * UINT64_C(0xbf58476d1ce4e5b9);
        int ply;

        set_starting_position(&position);
        for (ply = 0; ply < max_plies; ++ply) {
            MoveList moves;
            Move move;
            UndoState undo;
            char fen[128];

            generate_legal_moves(&position, &moves);
            if (moves.count <= 0 ||
                position.halfmove_clock >= 100 ||
                position_has_insufficient_material(&position)) {
                break;
            }
            if (!position_to_fen(&position, fen, sizeof(fen))) {
                fprintf(stderr, "Error: Could not write FEN\n");
                return 0;
            }
            output << "0.5 " << fen << "\n";

            move = moves.moves[
                next_random_value(&seed) % (uint64_t)moves.count
            ];
            if (!make_move(&position, move, &undo)) {
                fprintf(stderr, "Error: Random dataset move failed\n");
                return 0;
            }
        }
    }

    std::cout << "random dataset " << output_path << " games " << games
              << " plies " << max_plies << "\n";
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

static int parse_positive_int(
    const char *text,
    int minimum,
    int maximum,
    int *value
) {
    char *end;
    long parsed;

    if (text == 0 || value == 0) {
        return 0;
    }

    parsed = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        return 0;
    }

    *value = (int)parsed;
    return 1;
}

static int parse_positive_double(const char *text, double *value) {
    char *end;
    double parsed;

    if (text == 0 || value == 0) {
        return 0;
    }

    parsed = strtod(text, &end);
    if (*text == '\0' || *end != '\0' || parsed <= 0.0) {
        return 0;
    }

    *value = parsed;
    return 1;
}

int main(int argc, char **argv) {
    int depth;
    int time_limit_ms;
    const char *config_override;

    config_override = getenv("THETA_CONFIG_FILE");
    if (!load_config(
            config_override != 0 && *config_override != '\0'
                ? config_override
                : CONFIG_FILE
        )) {
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

    if (argc == 3 && strcmp(argv[1], "eval") == 0) {
        return print_evaluation_trace(argv[2]) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc == 3 && strcmp(argv[1], "features") == 0) {
        return print_eval_features_json(argv[2])
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
    }

    if (argc == 2 && strcmp(argv[1], "evalparams") == 0) {
        return print_eval_params_json() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc == 6 && strcmp(argv[1], "texel") == 0) {
        int iterations;
        double learning_rate;

        if (!parse_positive_int(argv[3], 1, 100000, &iterations) ||
            !parse_positive_double(argv[4], &learning_rate)) {
            fprintf(stderr, "Error: Invalid Texel arguments\n");
            return EXIT_FAILURE;
        }

        return run_texel_tuning(
            argv[2],
            iterations,
            learning_rate,
            argv[5]
        ) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc == 6 && strcmp(argv[1], "detailedtexel") == 0) {
        int iterations;
        double learning_rate;

        if (!parse_positive_int(argv[3], 1, 100000, &iterations) ||
            !parse_positive_double(argv[4], &learning_rate)) {
            fprintf(stderr, "Error: Invalid detailed Texel arguments\n");
            return EXIT_FAILURE;
        }

        return run_detailed_texel_tuning(
            argv[2],
            iterations,
            learning_rate,
            argv[5]
        ) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc == 6 && strcmp(argv[1], "phasetexel") == 0) {
        int iterations;
        double learning_rate;

        if (!parse_positive_int(argv[3], 1, 100000, &iterations) ||
            !parse_positive_double(argv[4], &learning_rate)) {
            fprintf(stderr, "Error: Invalid phase Texel arguments\n");
            return EXIT_FAILURE;
        }

        return run_phase_texel_tuning(
            argv[2],
            iterations,
            learning_rate,
            argv[5]
        ) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (argc == 3 && strcmp(argv[1], "phaseloss") == 0) {
        return print_phase_texel_loss(argv[2])
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
    }

    if (argc == 6 && strcmp(argv[1], "selfplaydata") == 0) {
        int games;
        int plies;

        if (!parse_positive_int(argv[2], 1, 10000, &games) ||
            !parse_depth(argv[3], &depth) ||
            !parse_positive_int(argv[4], 1, 1000, &plies)) {
            fprintf(stderr, "Error: Invalid self play data arguments\n");
            return EXIT_FAILURE;
        }

        return run_selfplay_dataset(games, depth, plies, argv[5])
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
    }

    if (argc == 5 && strcmp(argv[1], "randomdata") == 0) {
        int games;
        int plies;

        if (!parse_positive_int(argv[2], 1, 100000, &games) ||
            !parse_positive_int(argv[3], 1, 1000, &plies)) {
            fprintf(stderr, "Error: Invalid random data arguments\n");
            return EXIT_FAILURE;
        }

        return run_random_dataset(games, plies, argv[4])
            ? EXIT_SUCCESS
            : EXIT_FAILURE;
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
