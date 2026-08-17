#include <algorithm>
#include <stdio.h>
#include <vector>

#include "src/chess/fen.h"
#include "src/eval/nnue.cpp"

static void print_indices(const char *name, const std::vector<int> &values) {
    printf("%s %d", name, (int)values.size());
    for (int value : values) {
        printf(" %d", value);
    }
    printf("\n");
}

static void append_threats(
    const SfPositionData *data,
    int perspective,
    std::vector<int> *values
) {
    uint64_t pawn_targets = data->types[PIECE_TYPE_KNIGHT] |
        data->types[PIECE_TYPE_ROOK];
    uint64_t minor_slider_targets = data->types[PIECE_TYPE_PAWN] |
        data->types[PIECE_TYPE_KNIGHT] |
        data->types[PIECE_TYPE_BISHOP] |
        data->types[PIECE_TYPE_ROOK];
    uint64_t queen_targets = minor_slider_targets |
        data->types[PIECE_TYPE_QUEEN];

    for (int color = 0; color < 2; ++color) {
        int attacker_color = perspective ^ color;
        int pawn_code = PIECE_TYPE_PAWN +
            (attacker_color == COLOR_BLACK ? 8 : 0);
        uint64_t pawns = data->colors[attacker_color] &
            data->types[PIECE_TYPE_PAWN];
        while (pawns != 0) {
            int from = __builtin_ctzll(pawns);
            uint64_t attacks = sf_pseudo_attacks(
                PIECE_TYPE_PAWN,
                attacker_color,
                from
            ) & pawn_targets;
            pawns &= pawns - 1;
            while (attacks != 0) {
                int to = __builtin_ctzll(attacks);
                int index = sf_threat_index(
                    perspective,
                    pawn_code,
                    from,
                    to,
                    data->pieces[to],
                    data->kings[perspective]
                );
                if (index >= 0) {
                    values->push_back(index);
                }
                attacks &= attacks - 1;
            }
        }
        for (int type = PIECE_TYPE_KNIGHT;
             type < PIECE_TYPE_KING;
             ++type) {
            int attacker_code = type +
                (attacker_color == COLOR_BLACK ? 8 : 0);
            uint64_t pieces = data->colors[attacker_color] &
                data->types[type];
            uint64_t targets = type == PIECE_TYPE_KNIGHT ||
                type == PIECE_TYPE_QUEEN ? queen_targets :
                minor_slider_targets;
            while (pieces != 0) {
                int from = __builtin_ctzll(pieces);
                uint64_t attacks = sf_actual_attacks(
                    type,
                    attacker_color,
                    from,
                    data->occupied
                ) & targets;
                pieces &= pieces - 1;
                while (attacks != 0) {
                    int to = __builtin_ctzll(attacks);
                    int index = sf_threat_index(
                        perspective,
                        attacker_code,
                        from,
                        to,
                        data->pieces[to],
                        data->kings[perspective]
                    );
                    if (index >= 0) {
                        values->push_back(index);
                    }
                    attacks &= attacks - 1;
                }
            }
        }
    }
}

static void append_pairs(
    const SfPositionData *data,
    int perspective,
    std::vector<int> *values
) {
    int king = data->kings[perspective];
    uint64_t white = data->colors[COLOR_WHITE] &
        data->types[PIECE_TYPE_PAWN];
    uint64_t black = data->colors[COLOR_BLACK] &
        data->types[PIECE_TYPE_PAWN];
    uint64_t pawns = white;
    while (pawns != 0) {
        int from = __builtin_ctzll(pawns);
        uint64_t band;
        uint64_t partners;
        pawns &= pawns - 1;
        band = sf_pawn_pair_bb(from);
        partners = band & pawns;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);
            values->push_back(sf_pair_index(
                perspective,
                COLOR_WHITE,
                from,
                to,
                COLOR_WHITE,
                king
            ));
            partners &= partners - 1;
        }
        partners = band & black;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);
            values->push_back(sf_pair_index(
                perspective,
                COLOR_WHITE,
                from,
                to,
                COLOR_BLACK,
                king
            ));
            partners &= partners - 1;
        }
    }
    pawns = black;
    while (pawns != 0) {
        int from = __builtin_ctzll(pawns);
        uint64_t partners;
        pawns &= pawns - 1;
        partners = sf_pawn_pair_bb(from) & pawns;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);
            values->push_back(sf_pair_index(
                perspective,
                COLOR_BLACK,
                from,
                to,
                COLOR_BLACK,
                king
            ));
            partners &= partners - 1;
        }
    }
}

int main(int argc, char **argv) {
    Position position;
    SfPositionData data;

    if (argc < 2 || !position_from_fen(&position, argv[1])) {
        return 1;
    }
    build_sf_position(&position, &data);
    for (int perspective = 0; perspective < 2; ++perspective) {
        std::vector<int> psq;
        std::vector<int> threats;
        std::vector<int> pairs;
        for (int square = 0; square < 64; ++square) {
            int code = data.pieces[square];
            if (code != 0) {
                psq.push_back(sf_psq_index(
                    perspective,
                    square,
                    code,
                    data.kings[perspective]
                ));
            }
        }
        append_threats(&data, perspective, &threats);
        append_pairs(&data, perspective, &pairs);
        std::sort(psq.begin(), psq.end());
        std::sort(threats.begin(), threats.end());
        std::sort(pairs.begin(), pairs.end());
        printf("perspective %d\n", perspective);
        print_indices("psq", psq);
        print_indices("threats", threats);
        print_indices("pairs", pairs);
    }
    return 0;
}
