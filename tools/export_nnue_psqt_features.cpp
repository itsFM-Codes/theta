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
#include "src/eval/piece_square_tables.h"

int main(int argc, char **argv) {
    std::ifstream input;
    std::ofstream output;
    std::string line;
    int rows = 0;

    if (argc != 5) {
        std::cerr << "usage: export_nnue_psqt_features config network input output\n";
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
    for (int type = PIECE_TYPE_PAWN; type <= PIECE_TYPE_KING; ++type) {
        output << " psqt" << type << "_mg psqt" << type << "_eg";
    }
    output << "\n";

    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string ignored_result;
        std::string fen;
        Position position;
        EvalPhaseFeatureVector features = {};
        int psqt[PIECE_TYPE_KING + 1][2] = {};
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

        extract_eval_phase_features(&position, &features);
        {
            uint64_t pieces = position.occupied;

            while (pieces != 0) {
                int square = __builtin_ctzll(pieces);
                Piece piece = position_piece_at(&position, square);
                PieceType type = piece_type(piece);
                int sign = piece_color(piece) == COLOR_WHITE ? 1 : -1;

                pieces &= pieces - 1;
                if (type < PIECE_TYPE_PAWN || type > PIECE_TYPE_KING) {
                    continue;
                }
                psqt[type][0] += sign * piece_square_value(piece, square, 0);
                psqt[type][1] += sign * piece_square_value(piece, square, 256);
            }
        }

        side = position.side_to_move == COLOR_WHITE ? 1 : -1;
        output << score * side << " " << side << " "
               << features.endgame_weight;
        for (int index = 0; index < EVAL_FEATURE_COUNT; ++index) {
            output << " " << features.values[index][0]
                   << " " << features.values[index][1];
        }
        for (int type = PIECE_TYPE_PAWN; type <= PIECE_TYPE_KING; ++type) {
            output << " " << psqt[type][0] << " " << psqt[type][1];
        }
        output << "\n";
        rows++;
    }

    std::cout << "exported " << rows << " NNUE PSQT rows\n";
    return rows > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
