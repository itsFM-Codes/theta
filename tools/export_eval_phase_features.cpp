#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "src/chess/fen.h"
#include "src/config/config.h"
#include "src/eval/eval_features.h"
#include "src/eval/nnue.h"

static int parse_probability(const std::string &text, double *value) {
    char *end = 0;
    double parsed;

    if (value == 0) {
        return 0;
    }
    parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' ||
        parsed < 0.0 || parsed > 1.0) {
        return 0;
    }
    *value = parsed;
    return 1;
}

int main(int argc, char **argv) {
    std::ifstream input;
    std::ofstream output;
    std::string line;
    int rows = 0;

    if (argc != 3) {
        std::cerr << "usage: export_eval_phase_features input output\n";
        return EXIT_FAILURE;
    }
    if (!load_config("config/config.conf")) {
        return EXIT_FAILURE;
    }
    nnue_set_enabled(0);
    input.open(argv[1]);
    output.open(argv[2]);
    if (!input.is_open() || !output.is_open()) {
        std::cerr << "could not open feature files\n";
        return EXIT_FAILURE;
    }

    output << std::setprecision(10);
    output << "# result side endgame_weight";
    for (int index = 0; index < EVAL_FEATURE_COUNT; ++index) {
        output << " feature" << index << "_mg"
               << " feature" << index << "_eg";
    }
    output << " basis";
    for (int table = 0; table < 12; ++table) {
        for (int square = 0; square < SQUARE_COUNT; ++square) {
            output << " table" << table << "_" << square;
        }
    }
    output << "\n";

    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string result_text;
        std::string fen;
        double result;
        Position position;
        EvalPhaseFeatureVector features = {};

        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (!(stream >> result_text)) {
            continue;
        }
        std::getline(stream, fen);
        while (!fen.empty() && (fen[0] == ' ' || fen[0] == '\t')) {
            fen.erase(fen.begin());
        }
        if (!parse_probability(result_text, &result) ||
            !position_from_fen(&position, fen.c_str())) {
            continue;
        }

        extract_eval_phase_features(&position, &features);
        output << result << " "
               << (position.side_to_move == COLOR_WHITE ? 1 : -1) << " "
               << features.endgame_weight;
        for (int index = 0; index < EVAL_FEATURE_COUNT; ++index) {
            output << " " << features.values[index][0]
                   << " " << features.values[index][1];
        }
        output << " ";
        for (int table = 0; table < 12; ++table) {
            PieceType type;

            if (table < 5) {
                type = (PieceType)(PIECE_TYPE_PAWN + table);
            } else if (table < 10) {
                type = (PieceType)(PIECE_TYPE_PAWN + table - 5);
            } else {
                type = PIECE_TYPE_KING;
            }

            for (int square = 0; square < SQUARE_COUNT; ++square) {
                double basis = 0.0;

                for (int board_square = 0;
                     board_square < SQUARE_COUNT;
                     ++board_square) {
                    Piece piece = position_piece_at(&position, board_square);
                    int normalized_square = board_square;

                    if (piece_color(piece) == COLOR_WHITE) {
                        normalized_square ^= 56;
                    }
                    if (piece_type(piece) == type &&
                        normalized_square == square) {
                        basis += piece_color(piece) == COLOR_WHITE
                            ? 1.0
                            : -1.0;
                    }
                }
                output << " " << basis;
            }
        }
        output << "\n";
        rows++;
    }

    std::cout << "exported " << rows << " feature rows\n";
    return rows > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
