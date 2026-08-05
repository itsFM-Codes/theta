#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "src/chess/fen.h"
#include "src/chess/movegen.h"
#include "src/eval/nnue.h"

static void write_values(FILE *file, size_t count) {
    std::vector<float> values(count, 0.0f);
    assert(fwrite(values.data(), sizeof(float), count, file) == count);
}

static Move find_uci_move(Position *position, const char *text) {
    MoveList moves;
    int from = (text[0] - 'a') + (('8' - text[1]) * 8);
    int to = (text[2] - 'a') + (('8' - text[3]) * 8);
    Piece promotion = PIECE_NONE;
    int index;

    if (text[4] != '\0') {
        int white = position->side_to_move == COLOR_WHITE;
        switch (text[4]) {
            case 'q':
                promotion = white ? PIECE_WHITE_QUEEN : PIECE_BLACK_QUEEN;
                break;
            case 'r':
                promotion = white ? PIECE_WHITE_ROOK : PIECE_BLACK_ROOK;
                break;
            case 'b':
                promotion = white ? PIECE_WHITE_BISHOP : PIECE_BLACK_BISHOP;
                break;
            case 'n':
                promotion = white ? PIECE_WHITE_KNIGHT : PIECE_BLACK_KNIGHT;
                break;
            default:
                break;
        }
    }

    generate_moves(position, &moves);
    for (index = 0; index < moves.count; ++index) {
        Move move = moves.moves[index];
        if (move.from == from && move.to == to &&
            move.promotion == promotion) {
            return move;
        }
    }

    assert(0 && "test move was not generated");
    Move missing = {};
    return missing;
}

static void assert_incremental_sequence(
    const char *fen,
    const char *const *moves,
    int move_count
) {
    Position position;
    NnueState state;
    int index;

    assert(position_from_fen(&position, fen));
    assert(nnue_state_build(&position, &state));
    for (index = 0; index < move_count; ++index) {
        Move move = find_uci_move(&position, moves[index]);
        UndoState undo;
        NnueState child;
        int full_score;
        int incremental_score;

        assert(make_legal_move(&position, move, &undo));
        assert(nnue_state_update(
            &position,
            &move,
            &undo,
            &state,
            &child
        ));
        assert(nnue_evaluate(&position, &full_score));
        assert(nnue_evaluate_with_state(
            &position,
            &child,
            &incremental_score
        ));
        assert(full_score == incremental_score);
        state = child;
    }
}

int main(int argc, char **argv) {
    const char path[] = "build\\nnue-test.bin";
    const char magic[] = "THNNUE01";
    uint32_t version = 1;
    uint32_t input_features = THETA_NNUE_INPUT_FEATURES;
    uint32_t hidden_size = THETA_NNUE_HIDDEN_SIZE;
    uint32_t output_hidden_size = THETA_NNUE_OUTPUT_HIDDEN_SIZE;
    uint32_t reserved = 0;
    float output_scale = 1000.0f;
    float output_bias = 0.25f;
    FILE *file = fopen(path, "wb");
    Position position;
    int score = 0;

    if (argc > 1) {
        assert(position_from_fen(
            &position,
            argc > 2 ? argv[2]
                : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        ));
        assert(nnue_load(argv[1]));
        nnue_set_enabled(1);
        assert(nnue_evaluate(&position, &score));
        printf("stockfish_nnue_score %d\n", score);
        {
            static const char *const opening[] = {
                "e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6",
                "b5a4", "g8f6", "e1g1"
            };
            static const char *const special[] = {
                "e5d6", "e8c8", "e1g1"
            };
            static const char *const promotion[] = {"a7a8q"};

            assert_incremental_sequence(
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                opening,
                (int)(sizeof(opening) / sizeof(opening[0]))
            );
            assert_incremental_sequence(
                "r3k2r/8/8/3pP3/8/8/8/R3K2R w KQkq d6 0 1",
                special,
                (int)(sizeof(special) / sizeof(special[0]))
            );
            assert_incremental_sequence(
                "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
                promotion,
                (int)(sizeof(promotion) / sizeof(promotion[0]))
            );
        }
        nnue_unload();
        return 0;
    }

    assert(file != 0);
    assert(fwrite(magic, sizeof(char), 8, file) == 8);
    assert(fwrite(&version, sizeof(version), 1, file) == 1);
    assert(fwrite(&input_features, sizeof(input_features), 1, file) == 1);
    assert(fwrite(&hidden_size, sizeof(hidden_size), 1, file) == 1);
    assert(fwrite(
        &output_hidden_size,
        sizeof(output_hidden_size),
        1,
        file
    ) == 1);
    assert(fwrite(&reserved, sizeof(reserved), 1, file) == 1);
    assert(fwrite(&output_scale, sizeof(output_scale), 1, file) == 1);
    write_values(file, THETA_NNUE_INPUT_FEATURES * THETA_NNUE_HIDDEN_SIZE);
    write_values(file, THETA_NNUE_HIDDEN_SIZE);
    write_values(
        file,
        THETA_NNUE_HIDDEN_SIZE * THETA_NNUE_OUTPUT_HIDDEN_SIZE
    );
    write_values(file, THETA_NNUE_OUTPUT_HIDDEN_SIZE);
    write_values(file, THETA_NNUE_OUTPUT_HIDDEN_SIZE);
    assert(fwrite(&output_bias, sizeof(output_bias), 1, file) == 1);
    fclose(file);

    assert(position_from_fen(
        &position,
        "8/8/8/8/8/8/8/K6k w - - 0 1"
    ));
    assert(nnue_load(path));
    assert(nnue_is_loaded());
    nnue_set_enabled(1);
    assert(nnue_evaluate(&position, &score));
    assert(score == 250);
    nnue_set_enabled(0);
    assert(!nnue_evaluate(&position, &score));
    nnue_unload();
    remove(path);
    return 0;
}
