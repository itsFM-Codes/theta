#include "nnue.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#if defined(__AVX2__) || ( \
    defined(__GNUC__) && \
    (defined(__x86_64__) || defined(__i386__)) \
)
#include <immintrin.h>
#endif

#if defined(__GNUC__) && \
    (defined(__x86_64__) || defined(__i386__)) && \
    !defined(__AVX2__)
#define THETA_RUNTIME_AVX2 1
#define THETA_AVX2_TARGET __attribute__((target("avx2")))
#endif

#include "src/chess/move.h"
#include "src/chess/movegen.h"

static const char THETA_NNUE_MAGIC[8] = {
    'T', 'H', 'N', 'N', 'U', 'E', '0', '1'
};
static const uint32_t THETA_NNUE_VERSION = 1;

static const uint32_t STOCKFISH_NNUE_VERSION = UINT32_C(0x6A448AFA);
static const uint32_t STOCKFISH_NNUE_HASH = UINT32_C(0xA85B2205);
static const uint32_t STOCKFISH_FEATURE_HASH = UINT32_C(0xCB685313);
static const uint32_t STOCKFISH_ARCHITECTURE_HASH = UINT32_C(0x63337116);
static const int STOCKFISH_FEATURE_DIMENSIONS = 22528;
static const int STOCKFISH_THREAT_DIMENSIONS = 59808;
static const int STOCKFISH_PAIR_DIMENSIONS = 4560;
static const int STOCKFISH_TOTAL_AUX_DIMENSIONS =
    STOCKFISH_THREAT_DIMENSIONS + STOCKFISH_PAIR_DIMENSIONS;
static const int STOCKFISH_HALF_DIMENSIONS = 1024;
static const int STOCKFISH_BUCKETS = 8;
static const int STOCKFISH_FC0_OUTPUTS = 32;
static const int STOCKFISH_FC1_OUTPUTS = 32;

#if defined(THETA_RUNTIME_AVX2)

static int sf_runtime_avx2_available(void) {
    static const int available = []() {
        __builtin_cpu_init();
        return __builtin_cpu_supports("avx2") != 0;
    }();

    return available;
}

static THETA_AVX2_TARGET void sf_add_i16_weights_avx2(
    const int16_t *weights,
    int sign,
    int16_t *accumulator
) {
    int hidden;

    for (hidden = 0; hidden + 15 < STOCKFISH_HALF_DIMENSIONS; hidden += 16) {
        __m256i weight_values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(weights + hidden)
        );
        __m256i accumulator_values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(accumulator + hidden)
        );
        __m256i updated_values = sign > 0
            ? _mm256_add_epi16(accumulator_values, weight_values)
            : _mm256_sub_epi16(accumulator_values, weight_values);

        _mm256_storeu_si256(
            reinterpret_cast<__m256i *>(accumulator + hidden),
            updated_values
        );
    }
}

static THETA_AVX2_TARGET void sf_add_i8_weights_avx2(
    const int8_t *weights,
    int sign,
    int16_t *accumulator
) {
    int hidden;

    for (hidden = 0; hidden + 15 < STOCKFISH_HALF_DIMENSIONS; hidden += 16) {
        __m128i weight_values_8 = _mm_loadu_si128(
            reinterpret_cast<const __m128i *>(weights + hidden)
        );
        __m256i weight_values = _mm256_cvtepi8_epi16(weight_values_8);
        __m256i accumulator_values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(accumulator + hidden)
        );
        __m256i updated_values = sign > 0
            ? _mm256_add_epi16(accumulator_values, weight_values)
            : _mm256_sub_epi16(accumulator_values, weight_values);

        _mm256_storeu_si256(
            reinterpret_cast<__m256i *>(accumulator + hidden),
            updated_values
        );
    }
}

#endif

enum NnueKind {
    NNUE_KIND_NONE = 0,
    NNUE_KIND_THETA = 1,
    NNUE_KIND_STOCKFISH = 2
};

typedef struct StockfishStack {
    std::vector<int32_t> fc0_bias;
    std::vector<int8_t> fc0_weights;
    /*
     * FC0 is evaluated with Stockfish's block-sparse layout on AVX2.
     * The regular layout is retained for non-AVX2 fallback paths.
     */
    std::vector<int8_t> fc0_sparse_weights;
    std::vector<int32_t> fc1_bias;
    std::vector<int8_t> fc1_weights;
    std::vector<int32_t> fc2_bias;
    std::vector<int8_t> fc2_weights;
} StockfishStack;

typedef struct StockfishModel {
    std::vector<int16_t> biases;
    std::vector<int16_t> psq_weights;
    std::vector<int8_t> threat_weights;
    std::vector<int32_t> psqt_weights;
    std::vector<int32_t> threat_psqt_weights;
    std::array<StockfishStack, STOCKFISH_BUCKETS> stacks;
} StockfishModel;

typedef struct NnueModel {
    std::vector<float> input_weights;
    std::vector<float> hidden1_bias;
    std::vector<float> hidden2_weights;
    std::vector<float> hidden2_bias;
    std::vector<float> output_weights;
    float output_bias = 0.0f;
    float output_scale = 1000.0f;
    StockfishModel stockfish;
    int kind = NNUE_KIND_NONE;
    int loaded = 0;
    int enabled = 0;
    unsigned int generation = 1;
} NnueModel;

static NnueModel model;

static int read_data(FILE *file, void *data, size_t size, size_t count) {
    return file != 0 && data != 0 && fread(data, size, count, file) == count;
}

template<typename T>
static int read_raw_vector(FILE *file, std::vector<T> *values) {
    if (values == 0 || values->empty()) {
        return values != 0;
    }
    return read_data(file, values->data(), sizeof(T), values->size());
}

static int read_theta_vector(FILE *file, std::vector<float> *values) {
    return read_raw_vector(file, values);
}

static int read_sf_leb_vector(FILE *file, std::vector<int16_t> *values) {
    static const char MAGIC[] = "COMPRESSED_LEB128";
    char magic[sizeof(MAGIC) - 1];
    uint32_t byte_count;
    std::vector<unsigned char> bytes;
    size_t offset = 0;
    size_t index;

    if (file == 0 || values == 0 ||
        !read_data(file, magic, sizeof(magic), 1) ||
        !read_data(file, &byte_count, sizeof(byte_count), 1)) {
        return 0;
    }
    if (memcmp(magic, MAGIC, sizeof(magic)) != 0) {
        return 0;
    }

    bytes.resize(byte_count);
    if (!read_raw_vector(file, &bytes)) {
        return 0;
    }

    for (index = 0; index < values->size(); ++index) {
        uint32_t result = 0;
        unsigned int shift = 0;
        unsigned char byte = 0;

        do {
            if (offset >= bytes.size() || shift >= 35) {
                return 0;
            }
            byte = bytes[offset++];
            result |= uint32_t(byte & 0x7f) << shift;
            shift += 7;
        } while ((byte & 0x80) != 0);

        if ((byte & 0x40) != 0 && shift < 32) {
            result |= UINT32_MAX << shift;
        }
        (*values)[index] = static_cast<int16_t>(static_cast<int32_t>(result));
    }

    return offset == bytes.size();
}

static int read_sf_leb_vector(FILE *file, std::vector<int32_t> *values) {
    static const char MAGIC[] = "COMPRESSED_LEB128";
    char magic[sizeof(MAGIC) - 1];
    uint32_t byte_count;
    std::vector<unsigned char> bytes;
    size_t offset = 0;
    size_t index;

    if (file == 0 || values == 0 ||
        !read_data(file, magic, sizeof(magic), 1) ||
        !read_data(file, &byte_count, sizeof(byte_count), 1)) {
        return 0;
    }
    if (memcmp(magic, MAGIC, sizeof(magic)) != 0) {
        return 0;
    }

    bytes.resize(byte_count);
    if (!read_raw_vector(file, &bytes)) {
        return 0;
    }

    for (index = 0; index < values->size(); ++index) {
        uint32_t result = 0;
        unsigned int shift = 0;
        unsigned char byte = 0;

        do {
            if (offset >= bytes.size() || shift >= 35) {
                return 0;
            }
            byte = bytes[offset++];
            result |= uint32_t(byte & 0x7f) << shift;
            shift += 7;
        } while ((byte & 0x80) != 0);

        if ((byte & 0x40) != 0 && shift < 32) {
            result |= UINT32_MAX << shift;
        }
        (*values)[index] = static_cast<int32_t>(result);
    }

    return offset == bytes.size();
}

static int read_theta_model(const char *path, NnueModel *loaded) {
    FILE *file;
    char magic[8];
    uint32_t version;
    uint32_t input_features;
    uint32_t hidden_size;
    uint32_t output_hidden_size;
    uint32_t reserved;
    float output_scale;

    if (path == 0 || loaded == 0 || path[0] == '\0') {
        return 0;
    }

    file = fopen(path, "rb");
    if (file == 0) {
        return 0;
    }

    if (!read_data(file, magic, sizeof(char), sizeof(magic)) ||
        !read_data(file, &version, sizeof(version), 1) ||
        !read_data(file, &input_features, sizeof(input_features), 1) ||
        !read_data(file, &hidden_size, sizeof(hidden_size), 1) ||
        !read_data(file, &output_hidden_size, sizeof(output_hidden_size), 1) ||
        !read_data(file, &reserved, sizeof(reserved), 1) ||
        !read_data(file, &output_scale, sizeof(output_scale), 1) ||
        memcmp(magic, THETA_NNUE_MAGIC, sizeof(magic)) != 0 ||
        version != THETA_NNUE_VERSION ||
        input_features != THETA_NNUE_INPUT_FEATURES ||
        hidden_size != THETA_NNUE_HIDDEN_SIZE ||
        output_hidden_size != THETA_NNUE_OUTPUT_HIDDEN_SIZE ||
        reserved != 0 || output_scale <= 0.0f) {
        fclose(file);
        return 0;
    }

    loaded->input_weights.resize(
        THETA_NNUE_INPUT_FEATURES * THETA_NNUE_HIDDEN_SIZE
    );
    loaded->hidden1_bias.resize(THETA_NNUE_HIDDEN_SIZE);
    loaded->hidden2_weights.resize(
        THETA_NNUE_HIDDEN_SIZE * THETA_NNUE_OUTPUT_HIDDEN_SIZE
    );
    loaded->hidden2_bias.resize(THETA_NNUE_OUTPUT_HIDDEN_SIZE);
    loaded->output_weights.resize(THETA_NNUE_OUTPUT_HIDDEN_SIZE);
    loaded->output_bias = 0.0f;
    loaded->output_scale = output_scale;

    if (!read_theta_vector(file, &loaded->input_weights) ||
        !read_theta_vector(file, &loaded->hidden1_bias) ||
        !read_theta_vector(file, &loaded->hidden2_weights) ||
        !read_theta_vector(file, &loaded->hidden2_bias) ||
        !read_theta_vector(file, &loaded->output_weights) ||
        !read_data(file, &loaded->output_bias, sizeof(float), 1)) {
        fclose(file);
        return 0;
    }

    fclose(file);
    loaded->kind = NNUE_KIND_THETA;
    loaded->loaded = 1;
    return 1;
}

static int read_stockfish_header(FILE *file) {
    uint32_t version;
    uint32_t hash;
    uint32_t description_size;
    std::vector<char> description;

    if (!read_data(file, &version, sizeof(version), 1) ||
        !read_data(file, &hash, sizeof(hash), 1) ||
        !read_data(file, &description_size, sizeof(description_size), 1) ||
        version != STOCKFISH_NNUE_VERSION ||
        hash != STOCKFISH_NNUE_HASH || description_size > (1U << 20)) {
        return 0;
    }

    description.resize(description_size);
    return description.empty() ||
        read_data(file, description.data(), sizeof(char), description.size());
}

static int read_stockfish_stack(FILE *file, StockfishStack *stack) {
    int input_block;
    int output;
    int lane;

    if (file == 0 || stack == 0) {
        return 0;
    }
    stack->fc0_bias.resize(STOCKFISH_FC0_OUTPUTS);
    stack->fc0_weights.resize(STOCKFISH_FC0_OUTPUTS * STOCKFISH_HALF_DIMENSIONS);
    stack->fc0_sparse_weights.resize(
        STOCKFISH_HALF_DIMENSIONS * STOCKFISH_FC0_OUTPUTS
    );
    stack->fc1_bias.resize(STOCKFISH_FC1_OUTPUTS);
    stack->fc1_weights.resize(STOCKFISH_FC1_OUTPUTS * 64);
    stack->fc2_bias.resize(1);
    stack->fc2_weights.resize(128);

    if (!read_raw_vector(file, &stack->fc0_bias) ||
        !read_raw_vector(file, &stack->fc0_weights) ||
        !read_raw_vector(file, &stack->fc1_bias) ||
        !read_raw_vector(file, &stack->fc1_weights) ||
        !read_raw_vector(file, &stack->fc2_bias) ||
        !read_raw_vector(file, &stack->fc2_weights)) {
        return 0;
    }

    for (input_block = 0;
         input_block < STOCKFISH_HALF_DIMENSIONS / 4;
         ++input_block) {
        for (output = 0; output < STOCKFISH_FC0_OUTPUTS; ++output) {
            for (lane = 0; lane < 4; ++lane) {
                stack->fc0_sparse_weights[
                    (input_block * STOCKFISH_FC0_OUTPUTS + output) * 4 +
                    lane
                ] = stack->fc0_weights[
                    output * STOCKFISH_HALF_DIMENSIONS +
                    input_block * 4 + lane
                ];
            }
        }
    }
    return 1;
}

static int read_stockfish_model(const char *path, NnueModel *loaded) {
    FILE *file;
    uint32_t feature_hash;
    uint32_t architecture_hash;
    int bucket;
    int result;

    if (path == 0 || loaded == 0 || path[0] == '\0') {
        return 0;
    }

    file = fopen(path, "rb");
    if (file == 0) {
        return 0;
    }

    result = read_stockfish_header(file);
    if (result) {
        result = read_data(file, &feature_hash, sizeof(feature_hash), 1) &&
            feature_hash == STOCKFISH_FEATURE_HASH;
        loaded->stockfish.biases.resize(STOCKFISH_HALF_DIMENSIONS);
        loaded->stockfish.psq_weights.resize(
            STOCKFISH_FEATURE_DIMENSIONS * STOCKFISH_HALF_DIMENSIONS
        );
        loaded->stockfish.threat_weights.resize(
            STOCKFISH_TOTAL_AUX_DIMENSIONS * STOCKFISH_HALF_DIMENSIONS
        );
        loaded->stockfish.psqt_weights.resize(
            STOCKFISH_FEATURE_DIMENSIONS * STOCKFISH_BUCKETS
        );
        loaded->stockfish.threat_psqt_weights.resize(
            STOCKFISH_TOTAL_AUX_DIMENSIONS * STOCKFISH_BUCKETS
        );

        {
            std::vector<int32_t> threat_psqt_weights(STOCKFISH_THREAT_DIMENSIONS *
                                                     STOCKFISH_BUCKETS);
            std::vector<int8_t> pair_weights(STOCKFISH_PAIR_DIMENSIONS *
                                              STOCKFISH_HALF_DIMENSIONS);
            std::vector<int32_t> pair_psqt_weights(STOCKFISH_PAIR_DIMENSIONS *
                                                   STOCKFISH_BUCKETS);
            result = read_sf_leb_vector(file, &loaded->stockfish.biases) &&
                read_data(
                    file,
                    loaded->stockfish.threat_weights.data(),
                    sizeof(int8_t),
                    STOCKFISH_THREAT_DIMENSIONS * STOCKFISH_HALF_DIMENSIONS
                ) && read_sf_leb_vector(
                    file,
                    &threat_psqt_weights
                );
            if (result) {
                memcpy(
                    loaded->stockfish.threat_psqt_weights.data(),
                    threat_psqt_weights.data(),
                    threat_psqt_weights.size() * sizeof(int32_t)
                );
                loaded->stockfish.threat_weights.resize(
                    STOCKFISH_TOTAL_AUX_DIMENSIONS * STOCKFISH_HALF_DIMENSIONS
                );
                result = read_raw_vector(
                    file,
                    &pair_weights
                );
                if (result) {
                    memcpy(
                        loaded->stockfish.threat_weights.data() +
                            STOCKFISH_THREAT_DIMENSIONS * STOCKFISH_HALF_DIMENSIONS,
                        pair_weights.data(),
                        pair_weights.size() * sizeof(int8_t)
                    );
                }
            }
            result = result && read_sf_leb_vector(
                file,
                &pair_psqt_weights
            );
            if (result) {
                memcpy(
                    loaded->stockfish.threat_psqt_weights.data() +
                        STOCKFISH_THREAT_DIMENSIONS * STOCKFISH_BUCKETS,
                    pair_psqt_weights.data(),
                    pair_psqt_weights.size() * sizeof(int32_t)
                );
            }
            result = result && read_sf_leb_vector(
                file,
                &loaded->stockfish.psq_weights
            ) && read_sf_leb_vector(
                file,
                &loaded->stockfish.psqt_weights
            );
        }

        if (result) {
            for (bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
                result = read_data(
                    file,
                    &architecture_hash,
                    sizeof(architecture_hash),
                    1
                ) && architecture_hash == STOCKFISH_ARCHITECTURE_HASH &&
                    read_stockfish_stack(
                    file,
                    &loaded->stockfish.stacks[bucket]
                );
                if (!result) {
                    break;
                }
            }
        }
    }

    fclose(file);
    if (!result) {
        return 0;
    }

    loaded->kind = NNUE_KIND_STOCKFISH;
    loaded->loaded = 1;
    return 1;
}

static int feature_index(const Position *position, int square, Piece piece) {
    int relative_color;
    int type;

    if (position == 0 || !is_valid_square(square) ||
        piece == PIECE_NONE || position->side_to_move == COLOR_NONE) {
        return -1;
    }

    relative_color = piece_color(piece) == position->side_to_move ? 0 : 1;
    type = piece_type(piece) - PIECE_TYPE_PAWN;
    if (type < 0 || type >= 6) {
        return -1;
    }
    return (relative_color * 6 + type) * SQUARE_COUNT + square;
}

static void add_feature(
    float *accumulator,
    int feature,
    const std::vector<float> &weights
) {
    int index;

    if (accumulator == 0 || feature < 0 || feature >=
        THETA_NNUE_INPUT_FEATURES) {
        return;
    }

    for (index = 0; index < THETA_NNUE_HIDDEN_SIZE; ++index) {
        accumulator[index] += weights[
            feature * THETA_NNUE_HIDDEN_SIZE + index
        ];
    }
}

static int theta_evaluate(const Position *position, int *score) {
    float accumulator[THETA_NNUE_HIDDEN_SIZE];
    float hidden[THETA_NNUE_OUTPUT_HIDDEN_SIZE];
    float output;
    uint64_t pieces;
    int index;
    int result;

    for (index = 0; index < THETA_NNUE_HIDDEN_SIZE; ++index) {
        accumulator[index] = model.hidden1_bias[index];
    }

    pieces = position->occupied;
    while (pieces != 0) {
        int square = __builtin_ctzll(pieces);
        int feature = feature_index(
            position,
            square,
            position_piece_at(position, square)
        );

        add_feature(accumulator, feature, model.input_weights);
        pieces &= pieces - 1;
    }

    {
        int flags[4];
        if (position->side_to_move == COLOR_WHITE) {
            flags[0] = CASTLING_WHITE_KING_SIDE;
            flags[1] = CASTLING_WHITE_QUEEN_SIDE;
            flags[2] = CASTLING_BLACK_KING_SIDE;
            flags[3] = CASTLING_BLACK_QUEEN_SIDE;
        } else {
            flags[0] = CASTLING_BLACK_KING_SIDE;
            flags[1] = CASTLING_BLACK_QUEEN_SIDE;
            flags[2] = CASTLING_WHITE_KING_SIDE;
            flags[3] = CASTLING_WHITE_QUEEN_SIDE;
        }
        for (index = 0; index < 4; ++index) {
            if ((position->castling_rights & flags[index]) != 0) {
                add_feature(
                    accumulator,
                    768 + index,
                    model.input_weights
                );
            }
        }
    }

    if (is_valid_square(position->en_passant_square)) {
        add_feature(
            accumulator,
            772 + square_column(position->en_passant_square),
            model.input_weights
        );
    }

    for (index = 0; index < THETA_NNUE_HIDDEN_SIZE; ++index) {
        if (accumulator[index] < 0.0f) {
            accumulator[index] = 0.0f;
        }
    }

    for (index = 0; index < THETA_NNUE_OUTPUT_HIDDEN_SIZE; ++index) {
        int input_index;
        float value = model.hidden2_bias[index];

        for (input_index = 0;
             input_index < THETA_NNUE_HIDDEN_SIZE;
             ++input_index) {
            value += accumulator[input_index] * model.hidden2_weights[
                input_index * THETA_NNUE_OUTPUT_HIDDEN_SIZE + index
            ];
        }
        hidden[index] = value > 0.0f ? value : 0.0f;
    }

    output = model.output_bias;
    for (index = 0; index < THETA_NNUE_OUTPUT_HIDDEN_SIZE; ++index) {
        output += hidden[index] * model.output_weights[index];
    }
    result = (int)std::lround(output * model.output_scale);
    if (result > 30000) {
        result = 30000;
    } else if (result < -30000) {
        result = -30000;
    }
    *score = result;
    return 1;
}

typedef struct SfFeatureTables {
    int king_buckets[64];
    int map[6][6];
    int num_valid_targets[16];
    int piece_square_base[2][16];
    int offsets[16][64];
    int cumulative_piece_offsets[16];
    int cumulative_offsets[16];
} SfFeatureTables;

static int sf_popcount(uint64_t value) {
    return __builtin_popcountll(value);
}

static int sf_clamp(int value, int minimum, int maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static uint64_t sf_pseudo_attacks_raw(int type, int color, int square) {
    static const int KNIGHT_DELTAS[8][2] = {
        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
        {1, -2}, {1, 2}, {2, -1}, {2, 1}
    };
    static const int KING_DELTAS[8][2] = {
        {-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
        {0, 1}, {1, -1}, {1, 0}, {1, 1}
    };
    static const int SLIDER_DELTAS[8][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    uint64_t result = 0;
    int row = square / 8;
    int column = square % 8;
    int index;

    if (type == PIECE_TYPE_PAWN) {
        int row_delta = color == COLOR_WHITE ? 1 : -1;
        int column_delta;
        for (column_delta = -1; column_delta <= 1; column_delta += 2) {
            int next_row = row + row_delta;
            int next_column = column + column_delta;
            if (next_row >= 0 && next_row < 8 &&
                next_column >= 0 && next_column < 8) {
                result |= UINT64_C(1) << (next_row * 8 + next_column);
            }
        }
        return result;
    }

    if (type == PIECE_TYPE_KNIGHT || type == PIECE_TYPE_KING) {
        const int (*deltas)[2] = type == PIECE_TYPE_KNIGHT
            ? KNIGHT_DELTAS
            : KING_DELTAS;
        int count = type == PIECE_TYPE_KNIGHT ? 8 : 8;
        for (index = 0; index < count; ++index) {
            int next_row = row + deltas[index][0];
            int next_column = column + deltas[index][1];
            if (next_row >= 0 && next_row < 8 &&
                next_column >= 0 && next_column < 8) {
                result |= UINT64_C(1) << (next_row * 8 + next_column);
            }
        }
        return result;
    }

    {
        int first = type == PIECE_TYPE_BISHOP ? 0
            : type == PIECE_TYPE_ROOK ? 4 : 0;
        int last = type == PIECE_TYPE_BISHOP ? 3
            : type == PIECE_TYPE_ROOK ? 7 : 7;
        for (index = first; index <= last; ++index) {
            int next_row = row + SLIDER_DELTAS[index][0];
            int next_column = column + SLIDER_DELTAS[index][1];
            while (next_row >= 0 && next_row < 8 &&
                   next_column >= 0 && next_column < 8) {
                result |= UINT64_C(1) << (next_row * 8 + next_column);
                next_row += SLIDER_DELTAS[index][0];
                next_column += SLIDER_DELTAS[index][1];
            }
        }
    }
    return result;
}

static uint64_t sf_pseudo_attacks(int type, int color, int square) {
    typedef std::array<std::array<std::array<uint64_t, 64>, 8>, 2>
        PseudoAttackTable;
    static const PseudoAttackTable tables = []() {
        PseudoAttackTable value = {};

        for (int table_color = COLOR_WHITE;
             table_color <= COLOR_BLACK;
             ++table_color) {
            for (int table_type = PIECE_TYPE_PAWN;
                 table_type <= PIECE_TYPE_KING;
                 ++table_type) {
                for (int table_square = 0;
                     table_square < 64;
                     ++table_square) {
                    value[table_color][table_type][table_square] =
                        sf_pseudo_attacks_raw(
                            table_type,
                            table_color,
                            table_square
                        );
                }
            }
        }
        return value;
    }();

    if (type < PIECE_TYPE_PAWN || type > PIECE_TYPE_KING ||
        color < COLOR_WHITE || color > COLOR_BLACK ||
        square < 0 || square >= 64) {
        return 0;
    }
    return tables[color][type][square];
}

static unsigned char sf_pseudo_attack_index(
    int type,
    int color,
    int from,
    int to
) {
    typedef std::array<
        std::array<std::array<std::array<unsigned char, 64>, 64>, 8>, 2
    > AttackIndexTable;
    static const AttackIndexTable tables = []() {
        AttackIndexTable value = {};

        for (int table_color = COLOR_WHITE;
             table_color <= COLOR_BLACK;
             ++table_color) {
            for (int table_type = PIECE_TYPE_PAWN;
                 table_type <= PIECE_TYPE_KING;
                 ++table_type) {
                for (int table_from = 0; table_from < 64; ++table_from) {
                    uint64_t attacks = sf_pseudo_attacks(
                        table_type,
                        table_color,
                        table_from
                    );

                    for (int table_to = 0; table_to < 64; ++table_to) {
                        uint64_t lower = table_to == 0
                            ? 0
                            : (UINT64_C(1) << table_to) - 1;
                        value[table_color][table_type][table_from][table_to] =
                            (unsigned char)__builtin_popcountll(
                                attacks & lower
                            );
                    }
                }
            }
        }
        return value;
    }();

    if (type < PIECE_TYPE_PAWN || type > PIECE_TYPE_KING ||
        color < COLOR_WHITE || color > COLOR_BLACK ||
        from < 0 || from >= 64 || to < 0 || to >= 64) {
        return 0;
    }
    return tables[color][type][from][to];
}

static uint64_t sf_actual_attacks(
    int type,
    int color,
    int square,
    uint64_t occupied
) {
    static const int SLIDER_DELTAS[8][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    uint64_t result;
    int row;
    int column;
    int first;
    int last;
    int index;

    if (type == PIECE_TYPE_PAWN || type == PIECE_TYPE_KNIGHT ||
        type == PIECE_TYPE_KING) {
        return sf_pseudo_attacks(type, color, square);
    }

    result = 0;
    row = square / 8;
    column = square % 8;
    first = type == PIECE_TYPE_BISHOP ? 0
        : type == PIECE_TYPE_ROOK ? 4 : 0;
    last = type == PIECE_TYPE_BISHOP ? 3
        : type == PIECE_TYPE_ROOK ? 7 : 7;
    for (index = first; index <= last; ++index) {
        int next_row = row + SLIDER_DELTAS[index][0];
        int next_column = column + SLIDER_DELTAS[index][1];
        while (next_row >= 0 && next_row < 8 &&
               next_column >= 0 && next_column < 8) {
            int next_square = next_row * 8 + next_column;
            uint64_t mask = UINT64_C(1) << next_square;
            result |= mask;
            if ((occupied & mask) != 0) {
                break;
            }
            next_row += SLIDER_DELTAS[index][0];
            next_column += SLIDER_DELTAS[index][1];
        }
    }
    return result;
}

static const SfFeatureTables *sf_feature_tables(void) {
    static const SfFeatureTables tables = [] {
        SfFeatureTables value = {};
        static const int king_buckets[64] = {
            28 * 704, 29 * 704, 30 * 704, 31 * 704, 31 * 704, 30 * 704, 29 * 704, 28 * 704,
            24 * 704, 25 * 704, 26 * 704, 27 * 704, 27 * 704, 26 * 704, 25 * 704, 24 * 704,
            20 * 704, 21 * 704, 22 * 704, 23 * 704, 23 * 704, 22 * 704, 21 * 704, 20 * 704,
            16 * 704, 17 * 704, 18 * 704, 19 * 704, 19 * 704, 18 * 704, 17 * 704, 16 * 704,
            12 * 704, 13 * 704, 14 * 704, 15 * 704, 15 * 704, 14 * 704, 13 * 704, 12 * 704,
             8 * 704,  9 * 704, 10 * 704, 11 * 704, 11 * 704, 10 * 704,  9 * 704,  8 * 704,
             4 * 704,  5 * 704,  6 * 704,  7 * 704,  7 * 704,  6 * 704,  5 * 704,  4 * 704,
             0 * 704,  1 * 704,  2 * 704,  3 * 704,  3 * 704,  2 * 704,  1 * 704,  0 * 704
        };
        static const int threat_map[6][6] = {
            {-1, 0, -1, 1, -1, -1},
            {0, 1, 2, 3, 4, -1},
            {0, 1, 2, 3, -1, -1},
            {0, 1, 2, 3, -1, -1},
            {0, 1, 2, 3, 4, -1},
            {-1, -1, -1, -1, -1, -1}
        };
        int code;
        int perspective;
        int cumulative_offset = 0;

        memcpy(value.king_buckets, king_buckets, sizeof(king_buckets));
        memcpy(value.map, threat_map, sizeof(threat_map));
        for (code = 0; code < 16; ++code) {
            int type = code & 7;
            value.num_valid_targets[code] = type == PIECE_TYPE_PAWN ? 4
                : type == PIECE_TYPE_KNIGHT || type == PIECE_TYPE_QUEEN ? 10
                : type == PIECE_TYPE_BISHOP || type == PIECE_TYPE_ROOK ? 8
                : 0;
            for (perspective = 0; perspective < 2; ++perspective) {
                value.piece_square_base[perspective][code] = 0;
            }
        }

        for (perspective = 0; perspective < 2; ++perspective) {
            for (code = 1; code < 16; ++code) {
                int type = code & 7;
                int color = code >= 9 ? COLOR_BLACK : COLOR_WHITE;
                int relative_color;
                if (type < PIECE_TYPE_PAWN || type > PIECE_TYPE_KING) {
                    continue;
                }
                if (type == PIECE_TYPE_KING) {
                    value.piece_square_base[perspective][code] = 10 * 64;
                } else {
                    relative_color = color == perspective ? 0 : 1;
                    value.piece_square_base[perspective][code] =
                        ((type - 1) * 2 + relative_color) * 64;
                }
            }
        }

        for (code = 1; code < 16; ++code) {
            int type = code & 7;
            int color = code >= 9 ? COLOR_BLACK : COLOR_WHITE;
            int cumulative_piece_offset = 0;
            if (type < PIECE_TYPE_PAWN || type > PIECE_TYPE_KING) {
                continue;
            }
            for (int square = 0; square < 64; ++square) {
                value.offsets[code][square] = cumulative_piece_offset;
                if (type != PIECE_TYPE_PAWN ||
                    (square / 8 >= 1 && square / 8 <= 6)) {
                    cumulative_piece_offset += sf_popcount(
                        sf_pseudo_attacks(type, color, square)
                    );
                }
            }
            value.cumulative_piece_offsets[code] = cumulative_piece_offset;
            value.cumulative_offsets[code] = cumulative_offset;
            cumulative_offset += value.num_valid_targets[code] * cumulative_piece_offset;
        }
        return value;
    }();
    return &tables;
}

typedef NnuePositionData SfPositionData;

static int sf_piece_code(Piece piece) {
    PieceType type = piece_type(piece);
    Color color = piece_color(piece);
    if (type < PIECE_TYPE_PAWN || type > PIECE_TYPE_KING || color == COLOR_NONE) {
        return 0;
    }
    return (int)type + (color == COLOR_BLACK ? 8 : 0);
}

static void build_sf_position(const Position *position, SfPositionData *data) {
    int square;

    memset(data, 0, sizeof(*data));
    data->kings[COLOR_WHITE] = -1;
    data->kings[COLOR_BLACK] = -1;
    for (square = 0; square < 64; ++square) {
        int theta_square = square ^ 56;
        int code = sf_piece_code(position->board[theta_square]);
        int type = code & 7;
        int color = code >= 9 ? COLOR_BLACK : COLOR_WHITE;
        uint64_t mask = UINT64_C(1) << square;

        data->pieces[square] = (unsigned char)code;
        if (code == 0) {
            continue;
        }
        data->occupied |= mask;
        data->colors[color] |= mask;
        data->types[type] |= mask;
        if (type == PIECE_TYPE_KING) {
            data->kings[color] = square;
        }
    }
}

static int sf_psq_index(int perspective, int square, int code, int king) {
    const SfFeatureTables *tables = sf_feature_tables();
    int orientation;

    if (king < 0 || code <= 0 || code >= 16) {
        return -1;
    }
    orientation = (king & 7) < 4 ? 7 : 0;
    return (square ^ orientation ^ (56 * perspective)) +
        tables->piece_square_base[perspective][code] +
        tables->king_buckets[king ^ (56 * perspective)];
}

static int sf_threat_index(
    int perspective,
    int attacker,
    int from,
    int to,
    int attacked,
    int king
) {
    const SfFeatureTables *tables = sf_feature_tables();
    int orientation;
    int from_oriented;
    int to_oriented;
    int attacker_oriented;
    int attacked_oriented;
    int attacker_type;
    int attacked_type;
    int map;
    int semi_excluded;

    if (king < 0 || attacker <= 0 || attacked <= 0) {
        return -1;
    }
    orientation = ((king & 7) < 4 ? 0 : 7) ^ (56 * perspective);
    from_oriented = from ^ orientation;
    to_oriented = to ^ orientation;
    attacker_oriented = attacker ^ (8 * perspective);
    attacked_oriented = attacked ^ (8 * perspective);
    attacker_type = attacker_oriented & 7;
    attacked_type = attacked_oriented & 7;
    if (attacker_type < PIECE_TYPE_PAWN || attacker_type > PIECE_TYPE_KING ||
        attacked_type < PIECE_TYPE_PAWN || attacked_type > PIECE_TYPE_KING) {
        return -1;
    }

    map = tables->map[attacker_type - 1][attacked_type - 1];
    semi_excluded = attacker_type == attacked_type &&
        ((attacker ^ attacked) == 8 || attacker_type != PIECE_TYPE_PAWN);
    if (map < 0 || (from_oriented < to_oriented && semi_excluded)) {
        return -1;
    }

    return tables->cumulative_offsets[attacker_oriented] +
        (((attacked_oriented >= 9 ? COLOR_BLACK : COLOR_WHITE) *
          (tables->num_valid_targets[attacker_oriented] / 2)) + map) *
            tables->cumulative_piece_offsets[attacker_oriented] +
        tables->offsets[attacker_oriented][from_oriented] +
        sf_pseudo_attack_index(
            attacker_type,
            attacker_oriented >= 9 ? COLOR_BLACK : COLOR_WHITE,
            from_oriented,
            to_oriented
        );
}

static int sf_pair_index(
    int perspective,
    int color,
    int from,
    int to,
    int paired_color,
    int king
) {
    int orientation;
    int from_oriented;
    int to_oriented;
    int id_a;
    int id_b;
    int hi;
    int lo;

    if (king < 0) {
        return -1;
    }
    orientation = ((king & 7) < 4 ? 0 : 7) ^ (56 * perspective);
    from_oriented = from ^ orientation;
    to_oriented = to ^ orientation;
    id_a = 48 * (color ^ perspective) + from_oriented - 8;
    id_b = 48 * (paired_color ^ perspective) + to_oriented - 8;
    hi = std::max(id_a, id_b);
    lo = std::min(id_a, id_b);
    return hi * (hi - 1) / 2 + lo + STOCKFISH_THREAT_DIMENSIONS;
}

static void sf_add_psq(
    const StockfishModel *network,
    int index,
    int16_t *accumulator,
    int32_t *psqt
) {
    int hidden;
    int bucket;

    if (network == 0 || index < 0 || index >= STOCKFISH_FEATURE_DIMENSIONS ||
        accumulator == 0 || psqt == 0) {
        return;
    }

    const int16_t *weights = network->psq_weights.data() +
        index * STOCKFISH_HALF_DIMENSIONS;
    const int32_t *psqt_weights = network->psqt_weights.data() +
        index * STOCKFISH_BUCKETS;

#if defined(THETA_RUNTIME_AVX2)
    if (sf_runtime_avx2_available()) {
        sf_add_i16_weights_avx2(weights, 1, accumulator);
        for (bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
            psqt[bucket] += psqt_weights[bucket];
        }
        return;
    }
#endif

#if defined(__AVX2__)
    for (hidden = 0; hidden + 15 < STOCKFISH_HALF_DIMENSIONS;
         hidden += 16) {
        __m256i weight_values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(weights + hidden)
        );
        __m256i accumulator_values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(accumulator + hidden)
        );

        _mm256_storeu_si256(
            reinterpret_cast<__m256i *>(accumulator + hidden),
            _mm256_add_epi16(accumulator_values, weight_values)
        );
    }
#else
    for (hidden = 0; hidden < STOCKFISH_HALF_DIMENSIONS; ++hidden) {
        accumulator[hidden] += weights[hidden];
    }
#endif
    for (; hidden < STOCKFISH_HALF_DIMENSIONS; ++hidden) {
        accumulator[hidden] += weights[hidden];
    }
    for (bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
        psqt[bucket] += psqt_weights[bucket];
    }
}

static void sf_adjust_psq(
    const StockfishModel *network,
    int index,
    int sign,
    int16_t *accumulator,
    int32_t *psqt
) {
    const int16_t *weights;
    const int32_t *psqt_weights;
    int hidden;
    int bucket;

    if (network == 0 || index < 0 || index >= STOCKFISH_FEATURE_DIMENSIONS ||
        (sign != 1 && sign != -1) || accumulator == 0 || psqt == 0) {
        return;
    }

    weights = network->psq_weights.data() +
        index * STOCKFISH_HALF_DIMENSIONS;
    psqt_weights = network->psqt_weights.data() +
        index * STOCKFISH_BUCKETS;

#if defined(THETA_RUNTIME_AVX2)
    if (sf_runtime_avx2_available()) {
        sf_add_i16_weights_avx2(weights, sign, accumulator);
        for (bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
            psqt[bucket] += sign * psqt_weights[bucket];
        }
        return;
    }
#endif

#if defined(__AVX2__)
    for (hidden = 0; hidden + 15 < STOCKFISH_HALF_DIMENSIONS;
         hidden += 16) {
        __m256i weight_values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(weights + hidden)
        );
        __m256i accumulator_values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(accumulator + hidden)
        );
        __m256i updated_values = sign > 0
            ? _mm256_add_epi16(accumulator_values, weight_values)
            : _mm256_sub_epi16(accumulator_values, weight_values);

        _mm256_storeu_si256(
            reinterpret_cast<__m256i *>(accumulator + hidden),
            updated_values
        );
    }
#else
    for (hidden = 0; hidden < STOCKFISH_HALF_DIMENSIONS; ++hidden) {
        accumulator[hidden] += sign * weights[hidden];
    }
#endif
    for (; hidden < STOCKFISH_HALF_DIMENSIONS; ++hidden) {
        accumulator[hidden] += sign * weights[hidden];
    }
    for (bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
        psqt[bucket] += sign * psqt_weights[bucket];
    }
}

static void sf_adjust_aux(
    const StockfishModel *network,
    int index,
    int sign,
    int16_t *accumulator,
    int32_t *psqt
) {
    const int8_t *weights;
    const int32_t *psqt_weights;
    int hidden;
    int bucket;

    if (network == 0 || index < 0 ||
        index >= STOCKFISH_TOTAL_AUX_DIMENSIONS ||
        (sign != 1 && sign != -1) || accumulator == 0 || psqt == 0) {
        return;
    }
    weights = network->threat_weights.data() +
        index * STOCKFISH_HALF_DIMENSIONS;
    psqt_weights = network->threat_psqt_weights.data() +
        index * STOCKFISH_BUCKETS;

#if defined(THETA_RUNTIME_AVX2)
    if (sf_runtime_avx2_available()) {
        sf_add_i8_weights_avx2(weights, sign, accumulator);
        for (bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
            psqt[bucket] += sign * psqt_weights[bucket];
        }
        return;
    }
#endif
#if defined(__AVX2__)
    for (hidden = 0; hidden + 15 < STOCKFISH_HALF_DIMENSIONS;
         hidden += 16) {
        __m128i weight_values_8 = _mm_loadu_si128(
            reinterpret_cast<const __m128i *>(weights + hidden)
        );
        __m256i weight_values = _mm256_cvtepi8_epi16(weight_values_8);
        __m256i accumulator_values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(accumulator + hidden)
        );

        _mm256_storeu_si256(
            reinterpret_cast<__m256i *>(accumulator + hidden),
            sign > 0
                ? _mm256_add_epi16(accumulator_values, weight_values)
                : _mm256_sub_epi16(accumulator_values, weight_values)
        );
    }
#else
    for (hidden = 0; hidden < STOCKFISH_HALF_DIMENSIONS; ++hidden) {
        accumulator[hidden] += sign * weights[hidden];
    }
#endif
    for (; hidden < STOCKFISH_HALF_DIMENSIONS; ++hidden) {
        accumulator[hidden] += sign * weights[hidden];
    }
    for (bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
        psqt[bucket] += sign * psqt_weights[bucket];
    }
}

#define STOCKFISH_MAX_AUX_DELTAS 2048

typedef struct SfAuxDelta {
    int index;
    int sign;
} SfAuxDelta;

#if defined(THETA_RUNTIME_AVX2)

static THETA_AVX2_TARGET void sf_apply_aux_deltas_avx2(
    const StockfishModel *network,
    const SfAuxDelta *deltas,
    int count,
    int16_t *accumulator
) {
    int hidden;

    for (hidden = 0; hidden + 15 < STOCKFISH_HALF_DIMENSIONS; hidden += 16) {
        __m256i accumulator_values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(accumulator + hidden)
        );

        for (int delta = 0; delta < count; ++delta) {
            const SfAuxDelta *change = deltas + delta;
            const int8_t *weights = network->threat_weights.data() +
                change->index * STOCKFISH_HALF_DIMENSIONS + hidden;
            __m128i weight_values_8 = _mm_loadu_si128(
                reinterpret_cast<const __m128i *>(weights)
            );
            __m256i weight_values = _mm256_cvtepi8_epi16(weight_values_8);

            accumulator_values = change->sign > 0
                ? _mm256_add_epi16(accumulator_values, weight_values)
                : _mm256_sub_epi16(accumulator_values, weight_values);
        }
        _mm256_storeu_si256(
            reinterpret_cast<__m256i *>(accumulator + hidden),
            accumulator_values
        );
    }
}

#endif

static int sf_append_aux_delta(
    SfAuxDelta *deltas,
    int count,
    int capacity,
    int index,
    int sign
) {
    if (deltas == 0 || count < 0 || count > capacity ||
        index < 0 || index >= STOCKFISH_TOTAL_AUX_DIMENSIONS ||
        (sign != 1 && sign != -1)) {
        return count;
    }

#if !defined(THETA_DISABLE_AUX_DELTA_MERGE)
    /*
     * A move can remove and re-add the same threat feature while the local
     * dependency walk is being assembled.  Those opposite-signed entries
     * cancel exactly; remove them before the 1024-lane SIMD pass instead of
     * loading the same weight twice.  Same-signed duplicates stay separate.
     */
    for (int delta = 0; delta < count; ++delta) {
        if (deltas[delta].index == index &&
            deltas[delta].sign == -sign) {
            deltas[delta] = deltas[count - 1];
            return count - 1;
        }
    }
#endif

    if (count >= capacity) {
        return count;
    }
    deltas[count].index = index;
    deltas[count].sign = sign;
    return count + 1;
}

static void sf_apply_aux_deltas(
    const StockfishModel *network,
    const SfAuxDelta *deltas,
    int count,
    int16_t *accumulator,
    int32_t *psqt
) {
    int hidden;

    if (network == 0 || deltas == 0 || count <= 0 ||
        accumulator == 0 || psqt == 0) {
        return;
    }

#if defined(THETA_RUNTIME_AVX2)
    if (sf_runtime_avx2_available()) {
        sf_apply_aux_deltas_avx2(
            network,
            deltas,
            count,
            accumulator
        );
        for (int delta = 0; delta < count; ++delta) {
            const SfAuxDelta *change = deltas + delta;
            const int32_t *weights = network->threat_psqt_weights.data() +
                change->index * STOCKFISH_BUCKETS;
            for (int bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
                psqt[bucket] += change->sign * weights[bucket];
            }
        }
        return;
    }
#endif

#if defined(__AVX2__)
    for (hidden = 0; hidden + 15 < STOCKFISH_HALF_DIMENSIONS; hidden += 16) {
        __m256i accumulator_values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(accumulator + hidden)
        );

        for (int delta = 0; delta < count; ++delta) {
            const SfAuxDelta *change = deltas + delta;
            const int8_t *weights = network->threat_weights.data() +
                change->index * STOCKFISH_HALF_DIMENSIONS + hidden;
            __m128i weight_values_8 = _mm_loadu_si128(
                reinterpret_cast<const __m128i *>(weights)
            );
            __m256i weight_values = _mm256_cvtepi8_epi16(weight_values_8);

            accumulator_values = change->sign > 0
                ? _mm256_add_epi16(accumulator_values, weight_values)
                : _mm256_sub_epi16(accumulator_values, weight_values);
        }
        _mm256_storeu_si256(
            reinterpret_cast<__m256i *>(accumulator + hidden),
            accumulator_values
        );
    }
#else
    for (hidden = 0; hidden < STOCKFISH_HALF_DIMENSIONS; ++hidden) {
        int32_t delta_sum = 0;
        for (int delta = 0; delta < count; ++delta) {
            const SfAuxDelta *change = deltas + delta;
            delta_sum += change->sign * network->threat_weights[
                change->index * STOCKFISH_HALF_DIMENSIONS + hidden
            ];
        }
        accumulator[hidden] += delta_sum;
    }
#endif

    for (int delta = 0; delta < count; ++delta) {
        const SfAuxDelta *change = deltas + delta;
        const int32_t *weights = network->threat_psqt_weights.data() +
            change->index * STOCKFISH_BUCKETS;
        for (int bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
            psqt[bucket] += change->sign * weights[bucket];
        }
    }
}

/*
 * Reconstruct the parent Stockfish feature position from the child position
 * and the move undo data.  The search already did the make_move operation, so
 * copying the full Position and calling undo_move here needlessly repeats the
 * hash/cache bookkeeping on every NNUE child.  Keep this helper limited to
 * the feature-board state that the Stockfish accumulator needs.
 */
static void sf_set_position_piece(
    SfPositionData *data,
    int square,
    int code
) {
    uint64_t mask;
    int previous;

    if (data == 0 || !is_valid_square(square)) {
        return;
    }
    previous = data->pieces[square];
    if (previous == code) {
        return;
    }
    mask = UINT64_C(1) << square;
    if (previous != 0) {
        int previous_type = previous & 7;
        int previous_color = previous >= 9 ? COLOR_BLACK : COLOR_WHITE;

        data->occupied &= ~mask;
        data->colors[previous_color] &= ~mask;
        data->types[previous_type] &= ~mask;
        if (previous_type == PIECE_TYPE_KING &&
            data->kings[previous_color] == square) {
            data->kings[previous_color] = -1;
        }
    }

    data->pieces[square] = (unsigned char)code;
    if (code != 0) {
        int type = code & 7;
        int color = code >= 9 ? COLOR_BLACK : COLOR_WHITE;

        data->occupied |= mask;
        data->colors[color] |= mask;
        data->types[type] |= mask;
        if (type == PIECE_TYPE_KING) {
            data->kings[color] = square;
        }
    }
}

static void sf_apply_child_position(
    const SfPositionData *parent,
    const Move *move,
    const UndoState *undo,
    SfPositionData *child
) {
    int from;
    int to;
    Piece placed_piece;

    if (parent == 0 || move == 0 || undo == 0 || child == 0) {
        return;
    }

    *child = *parent;
    from = move->from ^ 56;
    to = move->to ^ 56;
    sf_set_position_piece(child, from, 0);
    if (undo->captured_piece != PIECE_NONE) {
        sf_set_position_piece(
            child,
            undo->captured_square ^ 56,
            0
        );
    }
    placed_piece = (move->flags & MOVE_FLAG_PROMOTION) != 0
        ? move->promotion
        : undo->moved_piece;
    sf_set_position_piece(child, to, sf_piece_code(placed_piece));

    if ((move->flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0 ||
        (move->flags & MOVE_FLAG_CASTLE_QUEENSIDE) != 0) {
        int row = square_row(move->from);
        int kingside = (move->flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0;
        int rook_from = make_square(row, kingside ? 7 : 0);
        int rook_to = make_square(row, kingside ? 5 : 3);
        Piece rook = piece_color(undo->moved_piece) == COLOR_WHITE
            ? PIECE_WHITE_ROOK
            : PIECE_BLACK_ROOK;

        sf_set_position_piece(child, rook_from ^ 56, 0);
        sf_set_position_piece(child, rook_to ^ 56, sf_piece_code(rook));
    }
}

static uint64_t sf_piece_threat_attacks(
    const SfPositionData *data,
    int square
) {
    int code;
    int type;
    int color;

    if (data == 0 || !is_valid_square(square)) {
        return 0;
    }
    code = data->pieces[square];
    type = code & 7;
    color = code >= 9 ? COLOR_BLACK : COLOR_WHITE;
    if (type < PIECE_TYPE_PAWN || type >= PIECE_TYPE_KING) {
        return 0;
    }
    return type == PIECE_TYPE_PAWN
        ? sf_pseudo_attacks(type, color, square)
        : sf_actual_attacks(type, color, square, data->occupied);
}

static uint64_t sf_threat_targets(
    const SfPositionData *data,
    int type
) {
    uint64_t minor_slider_targets;
    uint64_t queen_targets;

    if (data == 0) {
        return 0;
    }
    minor_slider_targets = data->types[PIECE_TYPE_PAWN] |
        data->types[PIECE_TYPE_KNIGHT] |
        data->types[PIECE_TYPE_BISHOP] |
        data->types[PIECE_TYPE_ROOK];
    queen_targets = minor_slider_targets |
        data->types[PIECE_TYPE_QUEEN];

    if (type == PIECE_TYPE_PAWN) {
        return data->types[PIECE_TYPE_KNIGHT] |
            data->types[PIECE_TYPE_ROOK];
    }
    if (type == PIECE_TYPE_KNIGHT || type == PIECE_TYPE_QUEEN) {
        return queen_targets;
    }
    if (type == PIECE_TYPE_BISHOP || type == PIECE_TYPE_ROOK) {
        return minor_slider_targets;
    }
    return 0;
}

static void sf_adjust_threats_piece(
    const StockfishModel *network,
    const SfPositionData *data,
    int perspective,
    int square,
    int sign,
    int16_t *accumulator,
    int32_t *psqt
) {
    int attacker_code;
    int type;
    int color;
    uint64_t attacks;
    uint64_t targets;

    if (network == 0 || data == 0 || perspective < 0 || perspective >= 2 ||
        !is_valid_square(square)) {
        return;
    }
    attacker_code = data->pieces[square];
    type = attacker_code & 7;
    if (type < PIECE_TYPE_PAWN || type >= PIECE_TYPE_KING) {
        return;
    }
    color = attacker_code >= 9 ? COLOR_BLACK : COLOR_WHITE;
    attacks = sf_piece_threat_attacks(data, square);
    targets = sf_threat_targets(data, type);
    attacks &= targets;
    while (attacks != 0) {
        int to = __builtin_ctzll(attacks);
        int index = sf_threat_index(
            perspective,
            attacker_code,
            square,
            to,
            data->pieces[to],
            data->kings[perspective]
        );

        sf_adjust_aux(network, index, sign, accumulator, psqt);
        attacks &= attacks - 1;
    }
    (void)color;
}

static int sf_append_threats_piece(
    const SfPositionData *data,
    int perspective,
    int square,
    int sign,
    SfAuxDelta *deltas,
    int count,
    int capacity
) {
    int attacker_code;
    int type;
    uint64_t attacks;
    uint64_t targets;

    if (data == 0 || perspective < 0 || perspective >= 2 ||
        !is_valid_square(square)) {
        return count;
    }
    attacker_code = data->pieces[square];
    type = attacker_code & 7;
    if (type < PIECE_TYPE_PAWN || type >= PIECE_TYPE_KING) {
        return count;
    }
    attacks = sf_piece_threat_attacks(data, square);
    targets = sf_threat_targets(data, type);
    attacks &= targets;
    while (attacks != 0) {
        int to = __builtin_ctzll(attacks);
        count = sf_append_aux_delta(
            deltas,
            count,
            capacity,
            sf_threat_index(
                perspective,
                attacker_code,
                square,
                to,
                data->pieces[to],
                data->kings[perspective]
            ),
            sign
        );
        attacks &= attacks - 1;
    }
    return count;
}

static void sf_adjust_threats(
    const StockfishModel *network,
    const SfPositionData *data,
    int perspective,
    int sign,
    int16_t *accumulator,
    int32_t *psqt
) {
    int color;

    for (color = 0; color < 2; ++color) {
        int attacker_color = perspective ^ color;
        for (int type = PIECE_TYPE_PAWN;
             type < PIECE_TYPE_KING;
             ++type) {
            uint64_t pieces = data->colors[attacker_color] &
                data->types[type];
            while (pieces != 0) {
                int square = __builtin_ctzll(pieces);

                pieces &= pieces - 1;
                sf_adjust_threats_piece(
                    network,
                    data,
                    perspective,
                    square,
                    sign,
                    accumulator,
                    psqt
                );
            }
        }
    }
}

static uint64_t sf_pawn_pair_bb(int square) {
    int file = square & 7;
    uint64_t files = UINT64_C(1) << file;
    uint64_t ranks = UINT64_C(0x00FFFFFFFFFFFF00);

    if (file > 0) {
        files |= UINT64_C(1) << (file - 1);
    }
    if (file < 7) {
        files |= UINT64_C(1) << (file + 1);
    }
    {
        uint64_t expanded = 0;
        for (int current_file = 0; current_file < 8; ++current_file) {
            if ((files & (UINT64_C(1) << current_file)) != 0) {
                expanded |= UINT64_C(0x0101010101010101) << current_file;
            }
        }
        files = expanded;
    }
    return files & ranks & ~(UINT64_C(1) << square);
}

static void sf_adjust_pairs(
    const StockfishModel *network,
    const SfPositionData *data,
    int perspective,
    int sign,
    int16_t *accumulator,
    int32_t *psqt
) {
    int king = data->kings[perspective];
    uint64_t white = data->colors[COLOR_WHITE] & data->types[PIECE_TYPE_PAWN];
    uint64_t black = data->colors[COLOR_BLACK] & data->types[PIECE_TYPE_PAWN];
    uint64_t pawns;

    pawns = white;
    while (pawns != 0) {
        int from = __builtin_ctzll(pawns);
        uint64_t band;
        uint64_t partners;
        pawns &= pawns - 1;
        band = sf_pawn_pair_bb(from);
        partners = band & pawns;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);
            sf_adjust_aux(
                network,
                sf_pair_index(perspective, COLOR_WHITE, from, to, COLOR_WHITE, king),
                sign,
                accumulator,
                psqt
            );
            partners &= partners - 1;
        }
        partners = band & black;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);
            sf_adjust_aux(
                network,
                sf_pair_index(perspective, COLOR_WHITE, from, to, COLOR_BLACK, king),
                sign,
                accumulator,
                psqt
            );
            partners &= partners - 1;
        }
    }

    pawns = black;
    while (pawns != 0) {
        int from = __builtin_ctzll(pawns);
        uint64_t band;
        uint64_t partners;
        pawns &= pawns - 1;
        band = sf_pawn_pair_bb(from);
        partners = band & pawns;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);
            sf_adjust_aux(
                network,
                sf_pair_index(perspective, COLOR_BLACK, from, to, COLOR_BLACK, king),
                sign,
                accumulator,
                psqt
            );
            partners &= partners - 1;
        }
    }
}

#define STOCKFISH_MAX_PAIR_INDICES 256

static int sf_collect_pair_indices(
    const SfPositionData *data,
    int perspective,
    int *indices,
    int capacity
) {
    int count = 0;
    int king;
    uint64_t white;
    uint64_t black;
    uint64_t pawns;

    if (data == 0 || indices == 0 || capacity <= 0 ||
        perspective < 0 || perspective >= 2) {
        return 0;
    }

    king = data->kings[perspective];
    white = data->colors[COLOR_WHITE] & data->types[PIECE_TYPE_PAWN];
    black = data->colors[COLOR_BLACK] & data->types[PIECE_TYPE_PAWN];
    pawns = white;
    while (pawns != 0) {
        int from = __builtin_ctzll(pawns);
        uint64_t partners;

        pawns &= pawns - 1;
        partners = sf_pawn_pair_bb(from) & pawns;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);

            if (count < capacity) {
                indices[count++] = sf_pair_index(
                    perspective,
                    COLOR_WHITE,
                    from,
                    to,
                    COLOR_WHITE,
                    king
                );
            }
            partners &= partners - 1;
        }
        partners = sf_pawn_pair_bb(from) & black;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);

            if (count < capacity) {
                indices[count++] = sf_pair_index(
                    perspective,
                    COLOR_WHITE,
                    from,
                    to,
                    COLOR_BLACK,
                    king
                );
            }
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

            if (count < capacity) {
                indices[count++] = sf_pair_index(
                    perspective,
                    COLOR_BLACK,
                    from,
                    to,
                    COLOR_BLACK,
                    king
                );
            }
            partners &= partners - 1;
        }
    }
    return count;
}

static int sf_pair_index_present(
    const int *indices,
    int count,
    int value
) {
    int index;

    for (index = 0; index < count; ++index) {
        if (indices[index] == value) {
            return 1;
        }
    }
    return 0;
}

static void sf_append_unique_pair_index(
    int *indices,
    int *count,
    int capacity,
    int value
) {
    if (indices == 0 || count == 0 || *count >= capacity || value < 0 ||
        sf_pair_index_present(indices, *count, value)) {
        return;
    }
    indices[*count] = value;
    (*count)++;
}

/*
 * A pawn-pair feature can change only when one of the two pawns changes
 * square or disappears.  The previous implementation rebuilt every active
 * pair on every pawn move and compared the two complete lists.  That is
 * correct, but it turns a local move into an O(pawns^2) update.  Collect only
 * pairs touching changed pawn squares instead; the final old/new comparison
 * remains deliberately unchanged so the feature set is exact.
 */
static void sf_collect_changed_pair_indices(
    const SfPositionData *data,
    int perspective,
    int square,
    int *indices,
    int *count,
    int capacity
) {
    int code;
    int king;
    uint64_t white;
    uint64_t black;
    uint64_t partners;

    if (data == 0 || indices == 0 || count == 0 ||
        perspective < 0 || perspective >= 2 || !is_valid_square(square)) {
        return;
    }
    code = data->pieces[square];
    if ((code & 7) != PIECE_TYPE_PAWN) {
        return;
    }

    king = data->kings[perspective];
    white = data->colors[COLOR_WHITE] & data->types[PIECE_TYPE_PAWN];
    black = data->colors[COLOR_BLACK] & data->types[PIECE_TYPE_PAWN];
    partners = sf_pawn_pair_bb(square);

    if (code < 9) {
        uint64_t same_color = partners & white;

        while (same_color != 0) {
            int to = __builtin_ctzll(same_color);

            sf_append_unique_pair_index(
                indices,
                count,
                capacity,
                sf_pair_index(
                    perspective,
                    COLOR_WHITE,
                    square,
                    to,
                    COLOR_WHITE,
                    king
                )
            );
            same_color &= same_color - 1;
        }

        partners &= black;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);

            sf_append_unique_pair_index(
                indices,
                count,
                capacity,
                sf_pair_index(
                    perspective,
                    COLOR_WHITE,
                    square,
                    to,
                    COLOR_BLACK,
                    king
                )
            );
            partners &= partners - 1;
        }
    } else {
        uint64_t white_partners = partners & white;

        while (white_partners != 0) {
            int from = __builtin_ctzll(white_partners);

            sf_append_unique_pair_index(
                indices,
                count,
                capacity,
                sf_pair_index(
                    perspective,
                    COLOR_WHITE,
                    from,
                    square,
                    COLOR_BLACK,
                    king
                )
            );
            white_partners &= white_partners - 1;
        }

        partners &= black;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);

            sf_append_unique_pair_index(
                indices,
                count,
                capacity,
                sf_pair_index(
                    perspective,
                    COLOR_BLACK,
                    square,
                    to,
                    COLOR_BLACK,
                    king
                )
            );
            partners &= partners - 1;
        }
    }
}

static int sf_update_pair_state(
    const SfPositionData *old_data,
    const SfPositionData *new_data,
    int perspective,
    uint64_t changed_squares,
    SfAuxDelta *deltas,
    int count,
    int capacity
) {
    int old_indices[STOCKFISH_MAX_PAIR_INDICES];
    int new_indices[STOCKFISH_MAX_PAIR_INDICES];
    int old_count;
    int new_count;
    uint64_t changed;
    int index;

    old_count = 0;
    new_count = 0;
    changed = changed_squares;
    while (changed != 0) {
        int square = __builtin_ctzll(changed);

        sf_collect_changed_pair_indices(
            old_data,
            perspective,
            square,
            old_indices,
            &old_count,
            STOCKFISH_MAX_PAIR_INDICES
        );
        sf_collect_changed_pair_indices(
            new_data,
            perspective,
            square,
            new_indices,
            &new_count,
            STOCKFISH_MAX_PAIR_INDICES
        );
        changed &= changed - 1;
    }
    for (index = 0; index < old_count; ++index) {
        if (!sf_pair_index_present(
                new_indices,
                new_count,
                old_indices[index]
            )) {
            count = sf_append_aux_delta(
                deltas,
                count,
                capacity,
                old_indices[index],
                -1
            );
        }
    }
    for (index = 0; index < new_count; ++index) {
        if (!sf_pair_index_present(
                old_indices,
                old_count,
                new_indices[index]
            )) {
            count = sf_append_aux_delta(
                deltas,
                count,
                capacity,
                new_indices[index],
                1
            );
        }
    }
    return count;
}

static uint64_t sf_reverse_threat_attackers(
    const SfPositionData *data,
    int target
) {
    static const int directions[8][2] = {
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}
    };
    uint64_t attackers = 0;
    int row;
    int column;
    int next_row;
    int next_column;
    int source;

    if (data == 0 || !is_valid_square(target)) {
        return 0;
    }
    row = target / 8;
    column = target & 7;

    /* Pawn attacks are directional, so they need an explicit inverse. */
    for (int color = COLOR_WHITE; color <= COLOR_BLACK; ++color) {
        int source_row = row + (color == COLOR_WHITE ? -1 : 1);
        if (source_row < 0 || source_row >= 8) {
            continue;
        }
        for (int delta = -1; delta <= 1; delta += 2) {
            int source_column = column + delta;
            if (source_column < 0 || source_column >= 8) {
                continue;
            }
            source = source_row * 8 + source_column;
            if (data->pieces[source] ==
                PIECE_TYPE_PAWN + (color == COLOR_BLACK ? 8 : 0)) {
                attackers |= UINT64_C(1) << source;
            }
        }
    }

    /* Knight attacks are symmetric. Kings are not NNUE threat attackers. */
    attackers |= sf_pseudo_attacks(
        PIECE_TYPE_KNIGHT,
        COLOR_WHITE,
        target
    ) & data->types[PIECE_TYPE_KNIGHT];

    /* The first occupied square on each ray is the only possible slider. */
    for (int direction = 0; direction < 8; ++direction) {
        next_row = row + directions[direction][0];
        next_column = column + directions[direction][1];
        while (next_row >= 0 && next_row < 8 &&
               next_column >= 0 && next_column < 8) {
            int square = next_row * 8 + next_column;
            uint64_t mask = UINT64_C(1) << square;

            if ((data->occupied & mask) != 0) {
                int type = data->pieces[square] & 7;
                int diagonal = direction < 4;
                if ((diagonal &&
                     (type == PIECE_TYPE_BISHOP ||
                      type == PIECE_TYPE_QUEEN)) ||
                    (!diagonal &&
                     (type == PIECE_TYPE_ROOK ||
                      type == PIECE_TYPE_QUEEN))) {
                    attackers |= mask;
                }
                break;
            }
            next_row += directions[direction][0];
            next_column += directions[direction][1];
        }
    }
    return attackers;
}

static uint64_t sf_affected_attackers(
    const SfPositionData *old_data,
    const SfPositionData *new_data,
    uint64_t changed_squares
) {
    uint64_t changed = changed_squares;
    uint64_t affected = 0;

    if (old_data == 0 || new_data == 0) {
        return 0;
    }
    while (changed != 0) {
        int square = __builtin_ctzll(changed);
        uint64_t square_mask = UINT64_C(1) << square;

        if (old_data->pieces[square] != 0 || new_data->pieces[square] != 0) {
            affected |= square_mask;
        }
        affected |= sf_reverse_threat_attackers(old_data, square);
        affected |= sf_reverse_threat_attackers(new_data, square);
        changed &= changed - 1;
    }
    return affected;
}

static void sf_update_aux_state(
    const StockfishModel *network,
    const SfPositionData *old_data,
    const SfPositionData *new_data,
    int perspective,
    uint64_t changed_squares,
    int pawn_structure_changed,
    int16_t *accumulator,
    int32_t *psqt
) {
    uint64_t affected;
    SfAuxDelta deltas[STOCKFISH_MAX_AUX_DELTAS];
    int count = 0;

    if (network == 0 || old_data == 0 || new_data == 0) {
        return;
    }
    affected = sf_affected_attackers(
        old_data,
        new_data,
        changed_squares
    );
    while (affected != 0) {
        int square = __builtin_ctzll(affected);

        if (old_data->pieces[square] != 0) {
            count = sf_append_threats_piece(
                old_data,
                perspective,
                square,
                -1,
                deltas,
                count,
                STOCKFISH_MAX_AUX_DELTAS
            );
        }
        if (new_data->pieces[square] != 0) {
            count = sf_append_threats_piece(
                new_data,
                perspective,
                square,
                1,
                deltas,
                count,
                STOCKFISH_MAX_AUX_DELTAS
            );
        }
        affected &= affected - 1;
    }

    if (pawn_structure_changed) {
        count = sf_update_pair_state(
            old_data,
            new_data,
            perspective,
            changed_squares,
            deltas,
            count,
            STOCKFISH_MAX_AUX_DELTAS
        );
    }
    sf_apply_aux_deltas(network, deltas, count, accumulator, psqt);
}

static uint8_t sf_sqr_activation(int value, int shift) {
    int64_t squared = (int64_t)value * value;
    int result = (int)(squared >> shift);
    return (uint8_t)std::min(result, 127);
}

static uint8_t sf_clip_activation(int value, int shift) {
    int result;
    if (value <= 0) {
        return 0;
    }
    result = value >> shift;
    return (uint8_t)sf_clamp(result, 0, 127);
}

#if defined(THETA_RUNTIME_AVX2)

static THETA_AVX2_TARGET void sf_affine_avx2(
    const std::vector<int32_t> &bias,
    const std::vector<int8_t> &weights,
    int input_size,
    int output_size,
    const uint8_t *input,
    int32_t *output
) {
    int out;

    for (out = 0; out < output_size; ++out) {
        const int8_t *weight_row = weights.data() + out * input_size;
        __m256i accumulator = _mm256_setzero_si256();
        const __m256i ones = _mm256_set1_epi16(1);
        int input_index;
        int32_t value = bias[out];

        for (input_index = 0; input_index + 31 < input_size;
             input_index += 32) {
            __m256i input_values = _mm256_loadu_si256(
                reinterpret_cast<const __m256i *>(input + input_index)
            );
            __m256i weight_values = _mm256_loadu_si256(
                reinterpret_cast<const __m256i *>(weight_row + input_index)
            );
            __m256i products = _mm256_maddubs_epi16(
                input_values,
                weight_values
            );
            __m256i pair_sums = _mm256_madd_epi16(products, ones);

            accumulator = _mm256_add_epi32(accumulator, pair_sums);
        }

        {
            alignas(32) int32_t partial[8];

            _mm256_store_si256(
                reinterpret_cast<__m256i *>(partial),
                accumulator
            );
            for (int lane = 0; lane < 8; ++lane) {
                value += partial[lane];
            }
        }

        for (; input_index < input_size; ++input_index) {
            value += (int32_t)weight_row[input_index] * input[input_index];
        }
        output[out] = value;
    }
}

#endif

#if defined(THETA_RUNTIME_AVX2) || defined(__AVX2__)

#if defined(THETA_RUNTIME_AVX2)
static THETA_AVX2_TARGET
#endif
void sf_affine_sparse_avx2(
    const std::vector<int32_t> &bias,
    const std::vector<int8_t> &weights,
    const uint8_t *input,
    int32_t *output
) {
    const __m256i ones = _mm256_set1_epi16(1);
    __m256i accumulators[STOCKFISH_FC0_OUTPUTS / 8];
    int output_group;
    int input_block;

    for (output_group = 0;
         output_group < STOCKFISH_FC0_OUTPUTS / 8;
         ++output_group) {
        accumulators[output_group] = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(
                bias.data() + output_group * 8
            )
        );
    }

    /*
     * The sparse layout stores four consecutive input weights for each
     * output.  A repeated 32-bit input value therefore feeds four
     * unsigned-by-signed products into each 32-bit output lane.
     */
    for (input_block = 0;
         input_block < STOCKFISH_HALF_DIMENSIONS / 4;
         ++input_block) {
        uint32_t packed_input;
        const int8_t *weight_block;
        __m256i input_values;

        memcpy(
            &packed_input,
            input + input_block * 4,
            sizeof(packed_input)
        );
        if (packed_input == 0) {
            continue;
        }

        input_values = _mm256_set1_epi32((int)packed_input);
        weight_block = weights.data() +
            input_block * STOCKFISH_FC0_OUTPUTS * 4;
        for (output_group = 0;
             output_group < STOCKFISH_FC0_OUTPUTS / 8;
             ++output_group) {
            const __m256i weight_values = _mm256_loadu_si256(
                reinterpret_cast<const __m256i *>(
                    weight_block + output_group * 8 * 4
                )
            );
            const __m256i products = _mm256_maddubs_epi16(
                input_values,
                weight_values
            );
            const __m256i sums = _mm256_madd_epi16(products, ones);

            accumulators[output_group] = _mm256_add_epi32(
                accumulators[output_group],
                sums
            );
        }
    }

    for (output_group = 0;
         output_group < STOCKFISH_FC0_OUTPUTS / 8;
         ++output_group) {
        _mm256_storeu_si256(
            reinterpret_cast<__m256i *>(output + output_group * 8),
            accumulators[output_group]
        );
    }
}

#endif

static void sf_affine(
    const std::vector<int32_t> &bias,
    const std::vector<int8_t> &weights,
    int input_size,
    int output_size,
    const uint8_t *input,
    int32_t *output
) {
#if defined(THETA_RUNTIME_AVX2)
    if (sf_runtime_avx2_available()) {
        sf_affine_avx2(
            bias,
            weights,
            input_size,
            output_size,
            input,
            output
        );
        return;
    }
#endif

    int out;
    for (out = 0; out < output_size; ++out) {
        int input_index;
        int32_t value = bias[out];

#if defined(__AVX2__)
        const int8_t *weight_row = weights.data() +
            out * input_size;
        __m256i accumulator = _mm256_setzero_si256();
        const __m256i ones = _mm256_set1_epi16(1);

        for (input_index = 0; input_index + 31 < input_size;
             input_index += 32) {
            __m256i input_values = _mm256_loadu_si256(
                reinterpret_cast<const __m256i *>(input + input_index)
            );
            __m256i weight_values = _mm256_loadu_si256(
                reinterpret_cast<const __m256i *>(weight_row + input_index)
            );
            __m256i products = _mm256_maddubs_epi16(
                input_values,
                weight_values
            );
            __m256i pair_sums = _mm256_madd_epi16(products, ones);

            accumulator = _mm256_add_epi32(accumulator, pair_sums);
        }

        {
            alignas(32) int32_t partial[8];

            _mm256_store_si256(
                reinterpret_cast<__m256i *>(partial),
                accumulator
            );
            for (int lane = 0; lane < 8; ++lane) {
                value += partial[lane];
            }
        }
#else
        for (input_index = 0; input_index < input_size; ++input_index) {
            value += (int32_t)weights[out * input_size + input_index] *
                input[input_index];
        }
#endif

        for (; input_index < input_size; ++input_index) {
            value += (int32_t)weights[out * input_size + input_index] *
                input[input_index];
        }
        output[out] = value;
    }
}

static int sf_propagate(
    const StockfishStack *stack,
    const uint8_t *input
) {
    int32_t fc0[STOCKFISH_FC0_OUTPUTS];
    int32_t fc1[STOCKFISH_FC1_OUTPUTS];
    int32_t fc2[1];
    uint8_t concat[128];
    int index;

#if defined(THETA_RUNTIME_AVX2)
    if (sf_runtime_avx2_available() &&
        stack->fc0_sparse_weights.size() ==
            (size_t)(STOCKFISH_HALF_DIMENSIONS * STOCKFISH_FC0_OUTPUTS)) {
        sf_affine_sparse_avx2(
            stack->fc0_bias,
            stack->fc0_sparse_weights,
            input,
            fc0
        );
    } else {
        sf_affine(
            stack->fc0_bias,
            stack->fc0_weights,
            STOCKFISH_HALF_DIMENSIONS,
            STOCKFISH_FC0_OUTPUTS,
            input,
            fc0
        );
    }
#elif defined(__AVX2__)
    if (stack->fc0_sparse_weights.size() ==
        (size_t)(STOCKFISH_HALF_DIMENSIONS * STOCKFISH_FC0_OUTPUTS)) {
        sf_affine_sparse_avx2(
            stack->fc0_bias,
            stack->fc0_sparse_weights,
            input,
            fc0
        );
    } else {
        sf_affine(
            stack->fc0_bias,
            stack->fc0_weights,
            STOCKFISH_HALF_DIMENSIONS,
            STOCKFISH_FC0_OUTPUTS,
            input,
            fc0
        );
    }
#else
    sf_affine(
        stack->fc0_bias,
        stack->fc0_weights,
        STOCKFISH_HALF_DIMENSIONS,
        STOCKFISH_FC0_OUTPUTS,
        input,
        fc0
    );
#endif
    for (index = 0; index < STOCKFISH_FC0_OUTPUTS; ++index) {
        concat[index] = sf_sqr_activation(fc0[index], 21);
        concat[STOCKFISH_FC0_OUTPUTS + index] = sf_clip_activation(fc0[index], 7);
    }

    sf_affine(
        stack->fc1_bias,
        stack->fc1_weights,
        64,
        STOCKFISH_FC1_OUTPUTS,
        concat,
        fc1
    );
    for (index = 0; index < STOCKFISH_FC1_OUTPUTS; ++index) {
        concat[64 + index] = sf_sqr_activation(fc1[index], 19);
        concat[96 + index] = sf_clip_activation(fc1[index], 6);
    }

    sf_affine(
        stack->fc2_bias,
        stack->fc2_weights,
        128,
        1,
        concat,
        fc2
    );
    fc2[0] += fc0[STOCKFISH_FC0_OUTPUTS - 2] -
        fc0[STOCKFISH_FC0_OUTPUTS - 1];
    return (int)(((int64_t)fc2[0] * 600 * 16) / (128 * 64 * 2)) / 16;
}

#if defined(THETA_RUNTIME_AVX2) || defined(__AVX2__)

#if defined(THETA_RUNTIME_AVX2)
static THETA_AVX2_TARGET
#endif
void sf_transform_accumulators_avx2(
    const int16_t *accumulator,
    uint8_t *output
) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i maximum = _mm256_set1_epi16(255);

    for (int square = 0; square < 512; square += 16) {
        __m256i first = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(accumulator + square)
        );
        __m256i second = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(accumulator + square + 512)
        );

        first = _mm256_max_epi16(zero, _mm256_min_epi16(first, maximum));
        second = _mm256_max_epi16(zero, _mm256_min_epi16(second, maximum));

        __m256i product = _mm256_srli_epi16(
            _mm256_mullo_epi16(first, second),
            9
        );
        __m128i product_low = _mm256_castsi256_si128(product);
        __m128i product_high = _mm256_extracti128_si256(product, 1);
        __m128i packed8 = _mm_packus_epi16(product_low, product_high);

        _mm_storeu_si128(
            reinterpret_cast<__m128i *>(output + square),
            packed8
        );
    }
}

#endif

static void sf_transform_accumulators(
    const int16_t *accumulator,
    uint8_t *output
) {
#if defined(THETA_RUNTIME_AVX2)
    if (sf_runtime_avx2_available()) {
        sf_transform_accumulators_avx2(accumulator, output);
        return;
    }
#elif defined(__AVX2__)
    sf_transform_accumulators_avx2(accumulator, output);
    return;
#endif

    for (int square = 0; square < 512; ++square) {
        int first = sf_clamp(accumulator[square], 0, 255);
        int second = sf_clamp(accumulator[square + 512], 0, 255);

        output[square] = (uint8_t)((first * second) / 512);
    }
}

static uint64_t sf_position_type_mask(
    const Position *position,
    int type
) {
    if (position == 0 || type < PIECE_TYPE_PAWN ||
        type > PIECE_TYPE_QUEEN) {
        return 0;
    }

    return position->piece_occupied[type] |
        position->piece_occupied[type +
            (PIECE_BLACK_PAWN - PIECE_WHITE_PAWN)];
}

static int stockfish_evaluate(
    const Position *position,
    const NnueState *state,
    int *score
) {
    SfPositionData data;
    int16_t accumulators[2][STOCKFISH_HALF_DIMENSIONS];
    int32_t psqt[2][STOCKFISH_BUCKETS];
    uint8_t transformed[STOCKFISH_HALF_DIMENSIONS];
    int perspective;
    int square;
    int bucket;
    int side;
    int other;
    int psqt_score;
    int positional_score;
    int nnue_score;
    int complexity;
    int material;
    int value;
    const StockfishStack *stack;
    const int16_t *accumulator_sources[2];
    const int32_t *psqt_sources[2];
    int use_state;

    use_state = state != 0 && state->valid &&
        state->generation == model.generation;
    if (use_state) {
        if (position == 0 || position->white_king_square == NO_SQUARE ||
            position->black_king_square == NO_SQUARE) {
            return 0;
        }
    } else {
        build_sf_position(position, &data);
    }
    if (!use_state &&
        (data.kings[COLOR_WHITE] < 0 || data.kings[COLOR_BLACK] < 0)) {
        return 0;
    }

    for (perspective = 0; perspective < 2; ++perspective) {
        if (use_state) {
            accumulator_sources[perspective] =
                state->psq_accumulators[perspective];
            psqt_sources[perspective] = state->psqt_accumulators[perspective];
        } else {
            for (square = 0; square < STOCKFISH_HALF_DIMENSIONS; ++square) {
                accumulators[perspective][square] =
                    model.stockfish.biases[square];
            }
            for (square = 0; square < STOCKFISH_BUCKETS; ++square) {
                psqt[perspective][square] = 0;
            }

            for (square = 0; square < 64; ++square) {
                int code = data.pieces[square];
                int index;
                if (code == 0) {
                    continue;
                }
                index = sf_psq_index(
                    perspective,
                    square,
                    code,
                    data.kings[perspective]
                );
                sf_add_psq(
                    &model.stockfish,
                    index,
                    accumulators[perspective],
                    psqt[perspective]
                );
            }

            sf_adjust_threats(
                &model.stockfish,
                &data,
                perspective,
                1,
                accumulators[perspective],
                psqt[perspective]
            );
            sf_adjust_pairs(
                &model.stockfish,
                &data,
                perspective,
                1,
                accumulators[perspective],
                psqt[perspective]
            );
            accumulator_sources[perspective] = accumulators[perspective];
            psqt_sources[perspective] = psqt[perspective];
        }
    }
    side = position->side_to_move;
    other = opposite_color(position->side_to_move);
    for (perspective = 0; perspective < 2; ++perspective) {
        int source = perspective == 0 ? side : other;
        int offset = perspective * 512;
        sf_transform_accumulators(
            accumulator_sources[source],
            transformed + offset
        );
    }

    bucket = (sf_popcount(use_state ? position->occupied : data.occupied) - 1) / 4;
    bucket = sf_clamp(bucket, 0, STOCKFISH_BUCKETS - 1);
    stack = &model.stockfish.stacks[bucket];
    positional_score = sf_propagate(stack, transformed);
    psqt_score = (psqt_sources[side][bucket] -
                  psqt_sources[other][bucket]) / 2 / 16;
    nnue_score = psqt_score + positional_score;
    complexity = std::abs(psqt_score - positional_score);
    nnue_score -= (int)((int64_t)nnue_score * complexity / 18236);
    material = 534 * sf_popcount(
        use_state ? sf_position_type_mask(position, PIECE_TYPE_PAWN) :
            data.types[PIECE_TYPE_PAWN]
    );
    material += 781 * sf_popcount(
        use_state ? sf_position_type_mask(position, PIECE_TYPE_KNIGHT) :
            data.types[PIECE_TYPE_KNIGHT]
    );
    material += 825 * sf_popcount(
        use_state ? sf_position_type_mask(position, PIECE_TYPE_BISHOP) :
            data.types[PIECE_TYPE_BISHOP]
    );
    material += 1276 * sf_popcount(
        use_state ? sf_position_type_mask(position, PIECE_TYPE_ROOK) :
            data.types[PIECE_TYPE_ROOK]
    );
    material += 2538 * sf_popcount(
        use_state ? sf_position_type_mask(position, PIECE_TYPE_QUEEN) :
            data.types[PIECE_TYPE_QUEEN]
    );
    value = (int)((int64_t)nnue_score * (77871 + material) / 77871);
    value -= value * position->halfmove_clock / 199;
    value = sf_clamp(value, -30000, 30000);
    *score = (int)((int64_t)value * 100 / 208);
    return 1;
}

static void sf_build_psq_state_perspective(
    const SfPositionData *data,
    int perspective,
    NnueState *state
) {
    int square;

    if (data == 0 || state == 0 || perspective < 0 || perspective >= 2) {
        return;
    }

    for (square = 0; square < STOCKFISH_HALF_DIMENSIONS; ++square) {
        state->psq_accumulators[perspective][square] =
            model.stockfish.biases[square];
    }
    for (square = 0; square < STOCKFISH_BUCKETS; ++square) {
        state->psqt_accumulators[perspective][square] = 0;
    }
    for (square = 0; square < 64; ++square) {
        int code = data->pieces[square];
        int index;

        if (code == 0) {
            continue;
        }
        index = sf_psq_index(
            perspective,
            square,
            code,
            data->kings[perspective]
        );
        sf_add_psq(
            &model.stockfish,
            index,
            state->psq_accumulators[perspective],
            state->psqt_accumulators[perspective]
        );
    }
}

static void sf_build_psq_state(
    const SfPositionData *data,
    NnueState *state
) {
    int perspective;

    if (data == 0 || state == 0) {
        return;
    }

    for (perspective = 0; perspective < 2; ++perspective) {
        sf_build_psq_state_perspective(data, perspective, state);
    }
    state->generation = model.generation;
    state->valid = 1;
}

static void sf_build_full_state(
    const SfPositionData *data,
    NnueState *state
) {
    int perspective;

    if (data == 0 || state == 0) {
        return;
    }
    state->position_data = *data;
    sf_build_psq_state(data, state);
    for (perspective = 0; perspective < 2; ++perspective) {
        sf_adjust_threats(
            &model.stockfish,
            data,
            perspective,
            1,
            state->psq_accumulators[perspective],
            state->psqt_accumulators[perspective]
        );
        sf_adjust_pairs(
            &model.stockfish,
            data,
            perspective,
            1,
            state->psq_accumulators[perspective],
            state->psqt_accumulators[perspective]
        );
    }
}

static void sf_adjust_psq_state_piece(
    const SfPositionData *data,
    int perspective,
    int theta_square,
    Piece piece,
    int sign,
    NnueState *state
) {
    int square;
    int code;
    int index;

    if (data == 0 || state == 0 || !is_valid_square(theta_square)) {
        return;
    }
    code = sf_piece_code(piece);
    if (code == 0) {
        return;
    }
    square = theta_square ^ 56;
    index = sf_psq_index(
        perspective,
        square,
        code,
        data->kings[perspective]
    );
    sf_adjust_psq(
        &model.stockfish,
        index,
        sign,
        state->psq_accumulators[perspective],
        state->psqt_accumulators[perspective]
    );
}

static void sf_update_psq_state(
    const Position *position,
    const Move *move,
    const UndoState *undo,
    const NnueState *parent,
    NnueState *child
) {
    SfPositionData data;
    SfPositionData old_data;
    uint64_t changed_squares;
    int pawn_structure_changed;
    int perspective;
    Piece placed_piece;
    Piece rook;
    int rook_from;
    int rook_to;
    int moving_king_perspective;

    if (position == 0 || move == 0 || undo == 0 || parent == 0 ||
        child == 0) {
        return;
    }

    if (!parent->valid || parent->generation != model.generation ||
        parent->position_data.kings[COLOR_WHITE] < 0 ||
        parent->position_data.kings[COLOR_BLACK] < 0) {
        build_sf_position(position, &data);
        if (data.kings[COLOR_WHITE] < 0 || data.kings[COLOR_BLACK] < 0) {
            child->valid = 0;
            return;
        }
        sf_build_full_state(&data, child);
        return;
    }

    old_data = parent->position_data;
    sf_apply_child_position(&old_data, move, undo, &data);
    if (old_data.kings[COLOR_WHITE] < 0 ||
        old_data.kings[COLOR_BLACK] < 0 ||
        data.kings[COLOR_WHITE] < 0 ||
        data.kings[COLOR_BLACK] < 0) {
        build_sf_position(position, &data);
        if (data.kings[COLOR_WHITE] < 0 || data.kings[COLOR_BLACK] < 0) {
            child->valid = 0;
            return;
        }
        sf_build_full_state(&data, child);
        return;
    }

    changed_squares = 0;
    if (is_valid_square(move->from)) {
        changed_squares |= UINT64_C(1) << (move->from ^ 56);
    }
    if (is_valid_square(move->to)) {
        changed_squares |= UINT64_C(1) << (move->to ^ 56);
    }
    if (is_valid_square(undo->captured_square)) {
        changed_squares |= UINT64_C(1) << (undo->captured_square ^ 56);
    }
    if ((move->flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0 ||
        (move->flags & MOVE_FLAG_CASTLE_QUEENSIDE) != 0) {
        int row = square_row(move->from);
        int kingside = (move->flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0;
        int rook_from = make_square(row, kingside ? 7 : 0);
        int rook_to = make_square(row, kingside ? 5 : 3);

        changed_squares |= UINT64_C(1) << (rook_from ^ 56);
        changed_squares |= UINT64_C(1) << (rook_to ^ 56);
    }
    pawn_structure_changed =
        piece_type(undo->moved_piece) == PIECE_TYPE_PAWN ||
        piece_type(undo->captured_piece) == PIECE_TYPE_PAWN;

    *child = *parent;
    child->position_data = data;
    child->generation = model.generation;
    child->valid = 1;
    placed_piece = (move->flags & MOVE_FLAG_PROMOTION) != 0
        ? move->promotion
        : undo->moved_piece;
    moving_king_perspective = piece_type(undo->moved_piece) ==
        PIECE_TYPE_KING ? piece_color(undo->moved_piece) : -1;

    rook_from = NO_SQUARE;
    rook_to = NO_SQUARE;
    rook = PIECE_NONE;
    if ((move->flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0 ||
        (move->flags & MOVE_FLAG_CASTLE_QUEENSIDE) != 0) {
        int row = square_row(move->from);
        int kingside = (move->flags & MOVE_FLAG_CASTLE_KINGSIDE) != 0;

        rook_from = make_square(row, kingside ? 7 : 0);
        rook_to = make_square(row, kingside ? 5 : 3);
        rook = piece_color(undo->moved_piece) == COLOR_WHITE
            ? PIECE_WHITE_ROOK
            : PIECE_BLACK_ROOK;
    }

    for (perspective = 0; perspective < 2; ++perspective) {
        if (perspective == moving_king_perspective) {
            sf_build_psq_state_perspective(&data, perspective, child);
            sf_adjust_threats(
                &model.stockfish,
                &data,
                perspective,
                1,
                child->psq_accumulators[perspective],
                child->psqt_accumulators[perspective]
            );
            sf_adjust_pairs(
                &model.stockfish,
                &data,
                perspective,
                1,
                child->psq_accumulators[perspective],
                child->psqt_accumulators[perspective]
            );
            continue;
        }

        sf_adjust_psq_state_piece(
            &data,
            perspective,
            move->from,
            undo->moved_piece,
            -1,
            child
        );
        if (undo->captured_piece != PIECE_NONE) {
            sf_adjust_psq_state_piece(
                &data,
                perspective,
                undo->captured_square,
                undo->captured_piece,
                -1,
                child
            );
        }
        sf_adjust_psq_state_piece(
            &data,
            perspective,
            move->to,
            placed_piece,
            1,
            child
        );
        if (rook != PIECE_NONE) {
            sf_adjust_psq_state_piece(
                &data,
                perspective,
                rook_from,
                rook,
                -1,
                child
            );
            sf_adjust_psq_state_piece(
                &data,
                perspective,
                rook_to,
                rook,
                1,
                child
            );
        }

#ifndef THETA_DISABLE_NNUE_AUX
        sf_update_aux_state(
            &model.stockfish,
            &old_data,
            &data,
            perspective,
            changed_squares,
            pawn_structure_changed,
            child->psq_accumulators[perspective],
            child->psqt_accumulators[perspective]
        );
#endif

    }
}

int nnue_state_build(const Position *position, NnueState *state) {
    SfPositionData data;

    if (!nnue_is_enabled() || model.kind != NNUE_KIND_STOCKFISH ||
        position == 0 || state == 0) {
        return 0;
    }
    build_sf_position(position, &data);
    if (data.kings[COLOR_WHITE] < 0 || data.kings[COLOR_BLACK] < 0) {
        state->valid = 0;
        return 0;
    }
    sf_build_full_state(&data, state);
    return 1;
}

int nnue_state_update(
    const Position *position,
    const Move *move,
    const UndoState *undo,
    const NnueState *parent,
    NnueState *child
) {
    if (!nnue_is_enabled() || model.kind != NNUE_KIND_STOCKFISH) {
        return 0;
    }
    sf_update_psq_state(position, move, undo, parent, child);
    return child != 0 && child->valid;
}

int nnue_evaluate_with_state(
    const Position *position,
    const NnueState *state,
    int *score
) {
    if (!nnue_is_enabled() || model.kind != NNUE_KIND_STOCKFISH ||
        state == 0 || !state->valid || state->generation != model.generation) {
        return 0;
    }
    return stockfish_evaluate(position, state, score);
}

int nnue_load(const char *path) {
    NnueModel loaded;

    if (path == 0 || path[0] == '\0') {
        return 0;
    }
    if (!read_theta_model(path, &loaded) &&
        !read_stockfish_model(path, &loaded)) {
        return 0;
    }

    loaded.generation = model.generation + 1;
    model = std::move(loaded);
    return 1;
}

void nnue_unload(void) {
    unsigned int generation = model.generation + 1;
    model = NnueModel();
    model.generation = generation;
}

void nnue_set_enabled(int enabled) {
    int next = enabled != 0 && model.loaded;

    if (model.enabled != next) {
        model.enabled = next;
        model.generation++;
    }
}

int nnue_is_enabled(void) {
    return model.loaded && model.enabled;
}

int nnue_is_loaded(void) {
    return model.loaded;
}

unsigned int nnue_generation(void) {
    return model.generation;
}

int nnue_evaluate(const Position *position, int *score) {
    if (!nnue_is_enabled() || position == 0 || score == 0) {
        return 0;
    }
    if (model.kind == NNUE_KIND_THETA) {
        return theta_evaluate(position, score);
    }
    if (model.kind == NNUE_KIND_STOCKFISH) {
        return stockfish_evaluate(position, 0, score);
    }
    return 0;
}
