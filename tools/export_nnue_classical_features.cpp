#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "src/chess/fen.h"
#include "src/config/config.h"
#include "src/eval/eval_features.h"
#include "src/eval/eval_params.h"
#include "src/eval/nnue.h"
#include "src/eval/strategic.h"
#include "src/eval/stockfish_classical.h"

static void clear_optional_terms(EvalParams *params) {
    params->advanced_safe_mobility_bonus = 0;
    params->advanced_coordination_bonus = 0;
    params->advanced_king_ring_bonus = 0;
    params->advanced_hanging_bonus = 0;
    params->advanced_passed_path_bonus = 0;
    params->stockfish_classical_scale = 0;
    params->stockfish_piece_scale = 0;
    params->stockfish_mobility_scale = 0;
    params->stockfish_king_scale = 0;
    params->stockfish_threat_scale = 0;
    params->stockfish_passed_scale = 0;
    params->stockfish_space_scale = 0;
    params->stockfish_classical_replace = 0;
}

int main(int argc, char **argv) {
    std::ifstream input;
    std::ofstream output;
    std::string line;
    int rows = 0;

    if (argc != 5) {
        std::cerr << "usage: export_nnue_classical_features config network input output\n";
        return EXIT_FAILURE;
    }
    if (!load_config(argv[1]) || !nnue_load(argv[2])) {
        std::cerr << "could not load config or NNUE network\n";
        return EXIT_FAILURE;
    }
    nnue_set_enabled(1);
    input.open(argv[3]);
    output.open(argv[4]);
    if (!input.is_open() || !output.is_open()) {
        std::cerr << "could not open training files\n";
        return EXIT_FAILURE;
    }

    output << std::setprecision(10);
    output << "# target_white_score side endgame_weight";
    for (int index = 0; index < EVAL_FEATURE_COUNT; ++index) {
        output << " feature" << index << "_mg"
               << " feature" << index << "_eg";
    }
    output << " advanced_safe_mobility advanced_coordination advanced_king_ring"
              " advanced_hanging advanced_passed_path"
              " stockfish_piece stockfish_mobility stockfish_king"
              " stockfish_threats stockfish_passed stockfish_space\n";

    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string ignored_result;
        std::string fen;
        Position position;
        EvalPhaseFeatureVector features = {};
        EvalParams base = *current_eval_params();
        int advanced[5] = {};
        StockfishClassicalBreakdown stockfish = {};
        int score;
        int side;

        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (!(stream >> ignored_result)) {
            continue;
        }
        std::getline(stream, fen);
        while (!fen.empty() && (fen[0] == ' ' || fen[0] == '\t')) {
            fen.erase(fen.begin());
        }
        if (!position_from_fen(&position, fen.c_str()) ||
            !nnue_evaluate(&position, &score)) {
            continue;
        }

        set_current_eval_params(&base);
        extract_eval_phase_features(&position, &features);

        for (int index = 0; index < 5; ++index) {
            EvalParams probe = base;

            clear_optional_terms(&probe);
            switch (index) {
                case 0: probe.advanced_safe_mobility_bonus = 1; break;
                case 1: probe.advanced_coordination_bonus = 1; break;
                case 2: probe.advanced_king_ring_bonus = 1; break;
                case 3: probe.advanced_hanging_bonus = 1; break;
                case 4: probe.advanced_passed_path_bonus = 1; break;
            }
            set_current_eval_params(&probe);
            advanced[index] = advanced_classical_score(&position, 0);
        }

        {
            EvalParams probe = base;

            clear_optional_terms(&probe);
            probe.stockfish_piece_scale = 256;
            probe.stockfish_mobility_scale = 256;
            probe.stockfish_king_scale = 256;
            probe.stockfish_threat_scale = 256;
            probe.stockfish_passed_scale = 256;
            probe.stockfish_space_scale = 256;
            set_current_eval_params(&probe);
            stockfish_classical_score_with_breakdown(
                &position,
                0,
                &stockfish
            );
        }

        side = position.side_to_move == COLOR_WHITE ? 1 : -1;
        output << score * side << " " << side << " "
               << features.endgame_weight;
        for (int index = 0; index < EVAL_FEATURE_COUNT; ++index) {
            output << " " << features.values[index][0]
                   << " " << features.values[index][1];
        }
        output << " " << advanced[0] << " " << advanced[1]
               << " " << advanced[2] << " " << advanced[3]
               << " " << advanced[4]
               << " " << stockfish.piece
               << " " << stockfish.mobility
               << " " << stockfish.king
               << " " << stockfish.threats
               << " " << stockfish.passed
               << " " << stockfish.space << "\n";
        rows++;
    }

    std::cout << "exported " << rows << " NNUE classical feature rows\n";
    return rows > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
