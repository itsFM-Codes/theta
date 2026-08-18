#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "src/chess/fen.h"
#include "src/config/config.h"
#include "src/eval/eval_features.h"
#include "src/eval/evaluation.h"
#include "src/eval/nnue.h"

static int parse_result(const std::string &text, double *result) {
    char *end = 0;
    double value;

    if (result == 0) {
        return 0;
    }
    value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || value < 0.0 || value > 1.0) {
        return 0;
    }
    *result = value;
    return 1;
}

int main(int argc, char **argv) {
    std::ifstream input;
    std::ofstream output;
    std::string line;
    int rows = 0;

    if (argc != 4) {
        std::cerr << "usage: export_classical_components config input output\n";
        return EXIT_FAILURE;
    }
    if (!load_config(argv[1])) {
        return EXIT_FAILURE;
    }
    nnue_set_enabled(0);
    input.open(argv[2]);
    output.open(argv[3]);
    if (!input.is_open() || !output.is_open()) {
        std::cerr << "could not open training files\n";
        return EXIT_FAILURE;
    }

    output << std::setprecision(10);
    output << "# result side endgame_weight white_score material piece_square "
              "mobility pawn_structure king_safety piece_activity threats "
              "space advanced stockfish stockfish_piece stockfish_mobility "
              "stockfish_king stockfish_threats stockfish_passed "
              "stockfish_space\n";

    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string result_text;
        std::string fen;
        double result;
        Position position;
        EvalPhaseFeatureVector phase_features = {};
        EvaluationTrace trace = {};
        int score;
        int side;

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
        if (!parse_result(result_text, &result) ||
            !position_from_fen(&position, fen.c_str())) {
            continue;
        }

        extract_eval_phase_features(&position, &phase_features);
        score = evaluate_position_with_trace(&position, &trace);
        side = position.side_to_move == COLOR_WHITE ? 1 : -1;

        output << result << " " << side << " "
               << phase_features.endgame_weight << " "
               << score * side << " "
               << trace.material_and_piece_square * side << " "
               << trace.mobility * side << " "
               << trace.pawn_structure * side << " "
               << trace.king_safety * side << " "
               << trace.piece_activity * side << " "
               << trace.threats * side << " "
               << trace.space * side << " "
               << trace.advanced * side << " "
               << trace.stockfish_classical * side << " "
               << trace.stockfish_piece * side << " "
               << trace.stockfish_mobility * side << " "
               << trace.stockfish_king * side << " "
               << trace.stockfish_threats * side << " "
               << trace.stockfish_passed * side << " "
               << trace.stockfish_space * side << "\n";
        rows++;
    }

    std::cout << "exported " << rows << " training rows\n";
    return rows > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
