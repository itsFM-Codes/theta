#include "nnue.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

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

enum NnueKind {
    NNUE_KIND_NONE = 0,
    NNUE_KIND_THETA = 1,
    NNUE_KIND_STOCKFISH = 2
};

typedef struct StockfishStack {
    std::vector<int32_t> fc0_bias;
    std::vector<int8_t> fc0_weights;
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
    stack->fc0_bias.resize(STOCKFISH_FC0_OUTPUTS);
    stack->fc0_weights.resize(STOCKFISH_FC0_OUTPUTS * STOCKFISH_HALF_DIMENSIONS);
    stack->fc1_bias.resize(STOCKFISH_FC1_OUTPUTS);
    stack->fc1_weights.resize(STOCKFISH_FC1_OUTPUTS * 64);
    stack->fc2_bias.resize(1);
    stack->fc2_weights.resize(128);

    return read_raw_vector(file, &stack->fc0_bias) &&
        read_raw_vector(file, &stack->fc0_weights) &&
        read_raw_vector(file, &stack->fc1_bias) &&
        read_raw_vector(file, &stack->fc1_weights) &&
        read_raw_vector(file, &stack->fc2_bias) &&
        read_raw_vector(file, &stack->fc2_weights);
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

static uint64_t sf_pseudo_attacks(int type, int color, int square) {
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

typedef struct SfPositionData {
    unsigned char pieces[64];
    uint64_t occupied;
    uint64_t colors[2];
    uint64_t types[8];
    int kings[2];
} SfPositionData;

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
    uint64_t pseudo;
    uint64_t lower;

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

    pseudo = sf_pseudo_attacks(
        attacker_type,
        attacker_oriented >= 9 ? COLOR_BLACK : COLOR_WHITE,
        from_oriented
    );
    lower = to_oriented == 0 ? 0 : (UINT64_C(1) << to_oriented) - 1;
    return tables->cumulative_offsets[attacker_oriented] +
        (((attacked_oriented >= 9 ? COLOR_BLACK : COLOR_WHITE) *
          (tables->num_valid_targets[attacker_oriented] / 2)) + map) *
            tables->cumulative_piece_offsets[attacker_oriented] +
        tables->offsets[attacker_oriented][from_oriented] +
        sf_popcount(pseudo & lower);
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
    int32_t *accumulator,
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

    for (hidden = 0; hidden < STOCKFISH_HALF_DIMENSIONS; ++hidden) {
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
    int32_t *accumulator,
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

    for (hidden = 0; hidden < STOCKFISH_HALF_DIMENSIONS; ++hidden) {
        accumulator[hidden] += sign * weights[hidden];
    }
    for (bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
        psqt[bucket] += sign * psqt_weights[bucket];
    }
}

static void sf_add_aux(
    const StockfishModel *network,
    int index,
    int32_t *accumulator,
    int32_t *psqt
) {
    const int8_t *weights;
    const int32_t *psqt_weights;
    int hidden;
    int bucket;

    if (index < 0 || index >= STOCKFISH_TOTAL_AUX_DIMENSIONS) {
        return;
    }
    weights = network->threat_weights.data() +
        index * STOCKFISH_HALF_DIMENSIONS;
    psqt_weights = network->threat_psqt_weights.data() +
        index * STOCKFISH_BUCKETS;
    for (hidden = 0; hidden < STOCKFISH_HALF_DIMENSIONS; ++hidden) {
        accumulator[hidden] += weights[hidden];
    }
    for (bucket = 0; bucket < STOCKFISH_BUCKETS; ++bucket) {
        psqt[bucket] += psqt_weights[bucket];
    }
}

static void sf_add_threats(
    const StockfishModel *network,
    const SfPositionData *data,
    int perspective,
    int32_t *accumulator,
    int32_t *psqt
) {
    const SfFeatureTables *tables = sf_feature_tables();
    uint64_t pawn_targets = data->types[PIECE_TYPE_KNIGHT] |
        data->types[PIECE_TYPE_ROOK];
    uint64_t minor_slider_targets = data->types[PIECE_TYPE_PAWN] |
        data->types[PIECE_TYPE_KNIGHT] |
        data->types[PIECE_TYPE_BISHOP] |
        data->types[PIECE_TYPE_ROOK];
    uint64_t queen_targets = minor_slider_targets |
        data->types[PIECE_TYPE_QUEEN];
    int color;

    (void)tables;
    for (color = 0; color < 2; ++color) {
        int attacker_color = perspective ^ color;
        int pawn_code = PIECE_TYPE_PAWN + (attacker_color == COLOR_BLACK ? 8 : 0);
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
                sf_add_aux(network, index, accumulator, psqt);
                attacks &= attacks - 1;
            }
        }

        for (int type = PIECE_TYPE_KNIGHT;
             type < PIECE_TYPE_KING;
             ++type) {
            int attacker_code = type +
                (attacker_color == COLOR_BLACK ? 8 : 0);
            uint64_t pieces = data->colors[attacker_color] & data->types[type];
            uint64_t targets = type == PIECE_TYPE_KNIGHT ||
                type == PIECE_TYPE_QUEEN ? queen_targets : minor_slider_targets;

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
                    sf_add_aux(network, index, accumulator, psqt);
                    attacks &= attacks - 1;
                }
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

static void sf_add_pairs(
    const StockfishModel *network,
    const SfPositionData *data,
    int perspective,
    int32_t *accumulator,
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
            sf_add_aux(
                network,
                sf_pair_index(perspective, COLOR_WHITE, from, to, COLOR_WHITE, king),
                accumulator,
                psqt
            );
            partners &= partners - 1;
        }
        partners = band & black;
        while (partners != 0) {
            int to = __builtin_ctzll(partners);
            sf_add_aux(
                network,
                sf_pair_index(perspective, COLOR_WHITE, from, to, COLOR_BLACK, king),
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
            sf_add_aux(
                network,
                sf_pair_index(perspective, COLOR_BLACK, from, to, COLOR_BLACK, king),
                accumulator,
                psqt
            );
            partners &= partners - 1;
        }
    }
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

static void sf_affine(
    const std::vector<int32_t> &bias,
    const std::vector<int8_t> &weights,
    int input_size,
    int output_size,
    const uint8_t *input,
    int32_t *output
) {
    int out;
    for (out = 0; out < output_size; ++out) {
        int input_index;
        int32_t value = bias[out];
        for (input_index = 0; input_index < input_size; ++input_index) {
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

    sf_affine(
        stack->fc0_bias,
        stack->fc0_weights,
        STOCKFISH_HALF_DIMENSIONS,
        STOCKFISH_FC0_OUTPUTS,
        input,
        fc0
    );
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

static int stockfish_evaluate(
    const Position *position,
    const NnueState *state,
    int *score
) {
    SfPositionData data;
    int32_t accumulators[2][STOCKFISH_HALF_DIMENSIONS];
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

    build_sf_position(position, &data);
    if (data.kings[COLOR_WHITE] < 0 || data.kings[COLOR_BLACK] < 0) {
        return 0;
    }

    for (perspective = 0; perspective < 2; ++perspective) {
        if (state != 0 && state->valid &&
            state->generation == model.generation) {
            memcpy(
                accumulators[perspective],
                state->psq_accumulators[perspective],
                sizeof(accumulators[perspective])
            );
            memcpy(
                psqt[perspective],
                state->psqt_accumulators[perspective],
                sizeof(psqt[perspective])
            );
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
        }
        sf_add_threats(
            &model.stockfish,
            &data,
            perspective,
            accumulators[perspective],
            psqt[perspective]
        );
        sf_add_pairs(
            &model.stockfish,
            &data,
            perspective,
            accumulators[perspective],
            psqt[perspective]
        );
    }
    side = position->side_to_move;
    other = opposite_color(position->side_to_move);
    for (perspective = 0; perspective < 2; ++perspective) {
        int source = perspective == 0 ? side : other;
        int offset = perspective * 512;
        for (square = 0; square < 512; ++square) {
            int first = sf_clamp(accumulators[source][square], 0, 255);
            int second = sf_clamp(accumulators[source][square + 512], 0, 255);
            transformed[offset + square] = (uint8_t)((first * second) / 512);
        }
    }

    bucket = (sf_popcount(data.occupied) - 1) / 4;
    bucket = sf_clamp(bucket, 0, STOCKFISH_BUCKETS - 1);
    stack = &model.stockfish.stacks[bucket];
    positional_score = sf_propagate(stack, transformed);
    psqt_score = (psqt[side][bucket] - psqt[other][bucket]) / 2 / 16;
    nnue_score = psqt_score + positional_score;
    complexity = std::abs(psqt_score - positional_score);
    nnue_score -= (int)((int64_t)nnue_score * complexity / 18236);
    material = 534 * sf_popcount(data.types[PIECE_TYPE_PAWN]);
    material += 781 * sf_popcount(data.types[PIECE_TYPE_KNIGHT]);
    material += 825 * sf_popcount(data.types[PIECE_TYPE_BISHOP]);
    material += 1276 * sf_popcount(data.types[PIECE_TYPE_ROOK]);
    material += 2538 * sf_popcount(data.types[PIECE_TYPE_QUEEN]);
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

    build_sf_position(position, &data);
    if (data.kings[COLOR_WHITE] < 0 || data.kings[COLOR_BLACK] < 0) {
        child->valid = 0;
        return;
    }

    if (!parent->valid || parent->generation != model.generation) {
        sf_build_psq_state(&data, child);
        return;
    }

    *child = *parent;
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
    sf_build_psq_state(&data, state);
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
