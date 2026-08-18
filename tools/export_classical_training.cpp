#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "src/chess/fen.h"
#include "src/config/config.h"
#include "src/eval/eval_params.h"
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

static int phase_value(const Position *position) {
    const EvalParams *params = current_eval_params();
    int phase = 0;
    uint64_t pieces;

    if (position == 0 || params->max_phase <= 0) {
        return 0;
    }

    pieces = position->occupied;
    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        PieceType type = piece_type(position_piece_at(position, square));

        pieces &= pieces - 1;
        if (type == PIECE_TYPE_KNIGHT || type == PIECE_TYPE_BISHOP) {
            phase++;
        } else if (type == PIECE_TYPE_ROOK) {
            phase += 2;
        } else if (type == PIECE_TYPE_QUEEN) {
            phase += 4;
        }
    }

    if (phase > params->max_phase) {
        phase = params->max_phase;
    }
    return (params->max_phase - phase) * 256 / params->max_phase;
}

int main(int argc, char **argv) {
    std::ifstream input;
    std::ofstream output;
    std::string line;
    int line_number = 0;
    int rows = 0;

    if (argc != 3) {
        std::cerr << "usage: export_classical_training input output\n";
        return EXIT_FAILURE;
    }
    if (!load_config("config/config.conf")) {
        return EXIT_FAILURE;
    }
    nnue_set_enabled(0);
    input.open(argv[1]);
    output.open(argv[2]);
    if (!input.is_open() || !output.is_open()) {
        std::cerr << "could not open training files\n";
        return EXIT_FAILURE;
    }

    output << "# result current_white_score endgame_weight fen\n";
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string result_text;
        std::string fen;
        double result;
        Position position;
        EvaluationTrace trace = {};
        int score;
        int white_score;

        line_number++;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (!(stream >> result_text)) {
            continue;
        }
        std::getline(stream, fen);
        if (!parse_result(result_text, &result)) {
            continue;
        }
        while (!fen.empty() && (fen[0] == ' ' || fen[0] == '\t')) {
            fen.erase(fen.begin());
        }
        if (!position_from_fen(&position, fen.c_str())) {
            std::cerr << "warning: invalid FEN on line " << line_number << "\n";
            continue;
        }

        score = evaluate_position_with_trace(&position, &trace);
        white_score = position.side_to_move == COLOR_BLACK ? -score : score;
        output << result << " " << white_score << " "
               << phase_value(&position) << " " << fen << "\n";
        rows++;
    }

    std::cout << "exported " << rows << " training rows\n";
    return rows > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
