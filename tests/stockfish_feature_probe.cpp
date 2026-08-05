#include <algorithm>
#include <stdio.h>
#include <string>
#include <vector>

#include "local/Stockfish/src/attacks.h"
#include "local/Stockfish/src/position.h"
#include "local/Stockfish/src/nnue/features/full_threats.h"
#include "local/Stockfish/src/nnue/features/half_ka_v2_hm.h"
#include "local/Stockfish/src/nnue/features/pp_3wide.h"

static void print_indices(const char *name, const uint16_t *begin, int count) {
    printf("%s %d", name, count);
    for (int index = 0; index < count; ++index) {
        printf(" %u", begin[index]);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    std::string fen = argc > 1
        ? argv[1]
        : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    Stockfish::Attacks::init();
    Stockfish::Position::init();
    Stockfish::StateInfo state;
    Stockfish::Position position;
    if (position.set(fen, false, &state).has_value()) {
        return 1;
    }
    for (Stockfish::Color perspective : {Stockfish::WHITE, Stockfish::BLACK}) {
        Stockfish::Eval::NNUE::Features::FullThreats::IndexList threats;
        Stockfish::Eval::NNUE::Features::PP_3Wide::IndexList pairs;
        std::vector<uint16_t> psq_values;
        std::vector<uint16_t> threat_values;
        std::vector<uint16_t> pair_values;

        for (Stockfish::Square square = Stockfish::SQ_A1;
             square <= Stockfish::SQ_H8;
             ++square) {
            Stockfish::Piece piece = position.piece_on(square);
            if (piece != Stockfish::NO_PIECE) {
                psq_values.push_back(
                    Stockfish::Eval::NNUE::Features::HalfKAv2_hm::make_index(
                        perspective,
                        square,
                        piece,
                        position.square<Stockfish::KING>(perspective)
                    )
                );
            }
        }
        Stockfish::Eval::NNUE::Features::FullThreats::append_active_indices(
            perspective,
            position,
            threats
        );
        Stockfish::Eval::NNUE::Features::PP_3Wide::append_active_indices(
            perspective,
            position,
            pairs
        );
        threat_values.assign(threats.begin(), threats.end());
        pair_values.assign(pairs.begin(), pairs.end());
        std::sort(psq_values.begin(), psq_values.end());
        std::sort(threat_values.begin(), threat_values.end());
        std::sort(pair_values.begin(), pair_values.end());
        printf("perspective %d\n", (int)perspective);
        print_indices("psq", psq_values.data(), (int)psq_values.size());
        print_indices("threats", threat_values.data(), (int)threat_values.size());
        print_indices("pairs", pair_values.data(), (int)pair_values.size());
    }
    return 0;
}
