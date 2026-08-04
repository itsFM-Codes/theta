#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "src/chess/fen.h"
#include "src/eval/nnue.h"

static void write_values(FILE *file, size_t count) {
    std::vector<float> values(count, 0.0f);
    assert(fwrite(values.data(), sizeof(float), count, file) == count);
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
