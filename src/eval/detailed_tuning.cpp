#include "eval_tuning.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "eval_params.h"
#include "eval_features.h"
#include "evaluation.h"
#include "nnue.h"
#include "src/chess/fen.h"

typedef struct DetailedParameter {
    const char *config_key;
    std::size_t offset;
    int minimum;
    int maximum;
} DetailedParameter;

typedef struct DetailedSample {
    double base_score;
    double result;
    std::vector<double> sensitivity;
} DetailedSample;

static const DetailedParameter DETAILED_PARAMETERS[] = {
    {"eval_pawn_value",
     offsetof(EvalParams, piece_values) +
         sizeof(int) * PIECE_TYPE_PAWN, 50, 200},
    {"eval_knight_value",
     offsetof(EvalParams, piece_values) +
         sizeof(int) * PIECE_TYPE_KNIGHT, 200, 450},
    {"eval_bishop_value",
     offsetof(EvalParams, piece_values) +
         sizeof(int) * PIECE_TYPE_BISHOP, 200, 450},
    {"eval_rook_value",
     offsetof(EvalParams, piece_values) +
         sizeof(int) * PIECE_TYPE_ROOK, 350, 700},
    {"eval_queen_value",
     offsetof(EvalParams, piece_values) +
         sizeof(int) * PIECE_TYPE_QUEEN, 700, 1200},

    /* Tune the scalar controls consumed by the production fast path.  The
     * phase-aware and optional Stockfish terms are deliberately excluded:
     * enabling them changes evaluator cost and needs a separate search
     * validation campaign. */
    {"eval_material_scale",
     offsetof(EvalParams, material_scale), 128, 384},
    {"eval_piece_square_scale",
     offsetof(EvalParams, piece_square_scale), 0, 512},
    {"eval_mobility_scale",
     offsetof(EvalParams, mobility_scale), 0, 512},
    {"eval_pawn_structure_scale",
     offsetof(EvalParams, pawn_structure_scale), 0, 512},
    {"eval_king_safety_scale",
     offsetof(EvalParams, king_safety_scale), 0, 512},
    {"eval_piece_activity_scale",
     offsetof(EvalParams, piece_activity_scale), 0, 512},
    {"eval_threat_scale",
     offsetof(EvalParams, threat_scale), 0, 512},
    {"eval_space_scale",
     offsetof(EvalParams, space_scale), 0, 512},
    {"eval_tempo_bonus",
     offsetof(EvalParams, tempo_bonus), -50, 50},

    {"eval_knight_mobility_weight",
     offsetof(EvalParams, knight_mobility_weight), 0, 8},
    {"eval_bishop_mobility_weight",
     offsetof(EvalParams, bishop_mobility_weight), 0, 8},
    {"eval_rook_mobility_weight",
     offsetof(EvalParams, rook_mobility_weight), 0, 8},
    {"eval_queen_mobility_weight",
     offsetof(EvalParams, queen_mobility_weight), 0, 8},

    {"eval_doubled_pawn_penalty",
     offsetof(EvalParams, doubled_pawn_penalty), 0, 50},
    {"eval_isolated_pawn_penalty",
     offsetof(EvalParams, isolated_pawn_penalty), 0, 50},
    {"eval_pawn_island_penalty",
     offsetof(EvalParams, pawn_island_penalty), 0, 50},
    {"eval_backward_pawn_penalty",
     offsetof(EvalParams, backward_pawn_penalty), 0, 50},
    {"eval_candidate_passed_pawn_bonus",
     offsetof(EvalParams, candidate_passed_pawn_bonus), 0, 80},
    {"eval_supported_passed_pawn_bonus",
     offsetof(EvalParams, supported_passed_pawn_bonus), 0, 80},
    {"eval_connected_passed_pawn_bonus",
     offsetof(EvalParams, connected_passed_pawn_bonus), 0, 80},
    {"eval_blocked_passed_pawn_penalty",
     offsetof(EvalParams, blocked_passed_pawn_penalty), 0, 80},
    {"eval_passed_pawn_king_distance_scale",
     offsetof(EvalParams, passed_pawn_king_distance_scale), 0, 10},

    {"eval_pawn_shield_bonus",
     offsetof(EvalParams, pawn_shield_bonus), 0, 50},
    {"eval_semi_open_file_penalty",
     offsetof(EvalParams, semi_open_file_penalty), 0, 50},
    {"eval_open_file_penalty",
     offsetof(EvalParams, open_file_penalty), 0, 50},
    {"eval_king_ring_attack_unit",
     offsetof(EvalParams, king_ring_attack_unit), 0, 20},
    {"eval_king_danger_quadratic_divisor",
     offsetof(EvalParams, king_danger_quadratic_divisor), 1, 100},
    {"eval_max_king_danger",
     offsetof(EvalParams, max_king_danger), 0, 400},
    {"eval_rook_file_pressure",
     offsetof(EvalParams, rook_file_pressure), 0, 50},
    {"eval_queen_file_pressure",
     offsetof(EvalParams, queen_file_pressure), 0, 80},

    {"eval_bishop_pair_middlegame_bonus",
     offsetof(EvalParams, bishop_pair_middlegame_bonus), 0, 100},
    {"eval_bishop_pair_endgame_bonus",
     offsetof(EvalParams, bishop_pair_endgame_bonus), 0, 120},
    {"eval_semi_open_rook_bonus",
     offsetof(EvalParams, semi_open_rook_bonus), 0, 80},
    {"eval_open_rook_bonus",
     offsetof(EvalParams, open_rook_bonus), 0, 80},
    {"eval_rook_seventh_rank_bonus",
     offsetof(EvalParams, rook_seventh_rank_bonus), 0, 80},
    {"eval_knight_outpost_bonus",
     offsetof(EvalParams, knight_outpost_bonus), 0, 80},
    {"eval_bad_bishop_pawn_penalty",
     offsetof(EvalParams, bad_bishop_pawn_penalty), 0, 30},
    {"eval_bad_bishop_mobility_penalty",
     offsetof(EvalParams, bad_bishop_mobility_penalty), 0, 60},
    {"eval_trapped_minor_penalty",
     offsetof(EvalParams, trapped_minor_penalty), 0, 80},

    {"eval_pawn_threat_base",
     offsetof(EvalParams, pawn_threat_base), 0, 80},
    {"eval_hanging_piece_divisor",
     offsetof(EvalParams, hanging_piece_divisor), 1, 100},
    {"eval_safe_space_bonus",
     offsetof(EvalParams, safe_space_bonus), 0, 20},

};

#define DETAILED_PARAMETER_COUNT \
    (sizeof(DETAILED_PARAMETERS) / sizeof(DETAILED_PARAMETERS[0]))

static int *parameter_at(EvalParams *params, std::size_t offset) {
    return reinterpret_cast<int *>(
        reinterpret_cast<unsigned char *>(params) + offset
    );
}

static int parameter_value(
    const EvalParams *params,
    const DetailedParameter &parameter
) {
    return *parameter_at(const_cast<EvalParams *>(params), parameter.offset);
}

static std::string trim_string(const std::string &text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    size_t last;

    if (first == std::string::npos) {
        return "";
    }

    last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

static int parse_result_token(const std::string &text, double *result) {
    char *end;
    double value;

    if (result == 0) {
        return 0;
    }
    if (text == "1" || text == "1.0" || text == "1-0") {
        *result = 1.0;
        return 1;
    }
    if (text == "0.5" || text == "1/2-1/2") {
        *result = 0.5;
        return 1;
    }
    if (text == "0" || text == "0.0" || text == "0-1") {
        *result = 0.0;
        return 1;
    }

    value = strtod(text.c_str(), &end);
    if (end != text.c_str() && *end == '\0' &&
        value >= 0.0 && value <= 1.0) {
        *result = value;
        return 1;
    }
    return 0;
}

static double score_from_white_perspective(const Position *position) {
    int score = evaluate_position_with_trace(position, 0);

    return position->side_to_move == COLOR_BLACK ? -score : score;
}

static double texel_probability(double score) {
    if (score > 2400.0) {
        return 0.9975;
    }
    if (score < -2400.0) {
        return 0.0025;
    }
    return 1.0 / (1.0 + std::exp(-score / 400.0));
}

static int load_detailed_dataset(
    const char *dataset_path,
    const EvalParams &base_params,
    std::vector<DetailedSample> *samples
) {
    std::ifstream file(dataset_path);
    std::string line;
    int line_number = 0;
    int old_nnue_enabled = nnue_is_enabled();

    if (!file.is_open() || samples == 0) {
        std::cerr << "Error: Could not open dataset " << dataset_path << "\n";
        return 0;
    }

    nnue_set_enabled(0);
    while (std::getline(file, line)) {
        std::string trimmed = trim_string(line);
        size_t separator;
        double result = 0.0;
        Position position;
        DetailedSample sample;
        int index;

        line_number++;
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        separator = trimmed.find_first_of(" \t");
        if (separator == std::string::npos ||
            !parse_result_token(
                trimmed.substr(0, separator),
                &result
            ) ||
            !position_from_fen(
                &position,
                trim_string(trimmed.substr(separator + 1)).c_str()
            )) {
            std::cerr << "Warning: Skipped dataset line "
                      << line_number << "\n";
            continue;
        }

        set_current_eval_params(&base_params);
        sample.base_score = score_from_white_perspective(&position);
        sample.result = result;
        sample.sensitivity.resize(DETAILED_PARAMETER_COUNT, 0.0);

        for (index = 0; index < (int)DETAILED_PARAMETER_COUNT; ++index) {
            const DetailedParameter &parameter = DETAILED_PARAMETERS[index];
            EvalParams plus_params = base_params;
            EvalParams minus_params = base_params;
            int original = parameter_value(&base_params, parameter);
            int plus = original < parameter.maximum
                ? original + 1
                : original;
            int minus = original > parameter.minimum
                ? original - 1
                : original;
            double plus_score;
            double minus_score;

            *parameter_at(&plus_params, parameter.offset) = plus;
            *parameter_at(&minus_params, parameter.offset) = minus;
            set_current_eval_params(&plus_params);
            plus_score = score_from_white_perspective(&position);
            set_current_eval_params(&minus_params);
            minus_score = score_from_white_perspective(&position);
            if (plus != minus) {
                sample.sensitivity[index] =
                    (plus_score - minus_score) / (double)(plus - minus);
            }
        }

        samples->push_back(sample);
    }
    set_current_eval_params(&base_params);
    nnue_set_enabled(old_nnue_enabled);
    return !samples->empty();
}

static int safe_parameter_radius(
    const DetailedParameter &parameter,
    int base_value
) {
    if (strstr(parameter.config_key, "advanced_") != 0) {
        return 32;
    }
    if (strcmp(parameter.config_key,
               "eval_stockfish_classical_scale") == 0) {
        return 256;
    }
    if (strcmp(parameter.config_key, "eval_tempo_bonus") == 0) {
        return 6;
    }
    if (strstr(parameter.config_key, "_scale") != 0) {
        return 32;
    }
    if (strstr(parameter.config_key, "mobility_weight") != 0) {
        return 1;
    }
    if (strstr(parameter.config_key, "_value") != 0) {
        return base_value > 0
            ? (base_value / 10 > 8 ? base_value / 10 : 8)
            : 8;
    }
    if (strstr(parameter.config_key, "divisor") != 0) {
        return base_value / 5 > 2 ? base_value / 5 : 2;
    }
    if (strcmp(parameter.config_key, "eval_max_king_danger") == 0) {
        return 40;
    }
    return base_value / 4 > 2 ? base_value / 4 : 2;
}

static void clamp_values(
    std::vector<double> *values,
    const std::vector<int> &minimums,
    const std::vector<int> &maximums
) {
    size_t index;

    if (values == 0) {
        return;
    }
    for (index = 0; index < DETAILED_PARAMETER_COUNT; ++index) {
        if ((*values)[index] < minimums[index]) {
            (*values)[index] = minimums[index];
        }
        if ((*values)[index] > maximums[index]) {
            (*values)[index] = maximums[index];
        }
    }
}

static double linear_score(
    const DetailedSample &sample,
    const std::vector<double> &values,
    const std::vector<double> &base_values
) {
    double score = sample.base_score;
    size_t index;

    for (index = 0; index < DETAILED_PARAMETER_COUNT; ++index) {
        score += sample.sensitivity[index] *
            (values[index] - base_values[index]);
    }
    return score;
}

static double detailed_loss(
    const std::vector<DetailedSample> &samples,
    const std::vector<double> &values,
    const std::vector<double> &base_values
) {
    double loss = 0.0;
    size_t index;

    for (index = 0; index < samples.size(); ++index) {
        double probability = texel_probability(
            linear_score(samples[index], values, base_values)
        );
        double error = probability - samples[index].result;

        loss += error * error;
    }
    return loss / samples.size();
}

static int write_detailed_config(
    const char *output_path,
    const std::vector<double> &values
) {
    std::ofstream output(output_path);
    size_t index;

    if (!output.is_open()) {
        std::cerr << "Error: Could not write " << output_path << "\n";
        return 0;
    }

    output << "# Theta detailed Texel tuning output\n";
    output << "# Paste these keys into config/config.conf\n";
    for (index = 0; index < DETAILED_PARAMETER_COUNT; ++index) {
        int value = values[index] >= 0.0
            ? (int)(values[index] + 0.5)
            : (int)(values[index] - 0.5);

        output << DETAILED_PARAMETERS[index].config_key
               << " = " << value << "\n";
    }
    return 1;
}

int run_detailed_texel_tuning(
    const char *dataset_path,
    int iterations,
    double learning_rate,
    const char *output_path
) {
    const EvalParams base_params = *current_eval_params();
    std::vector<DetailedSample> samples;
    std::vector<double> base_values(DETAILED_PARAMETER_COUNT);
    std::vector<double> values(DETAILED_PARAMETER_COUNT);
    std::vector<int> minimums(DETAILED_PARAMETER_COUNT);
    std::vector<int> maximums(DETAILED_PARAMETER_COUNT);
    int iteration;
    int report_interval;
    size_t index;

    if (dataset_path == 0 || output_path == 0 ||
        iterations <= 0 || learning_rate <= 0.0) {
        std::cerr << "Error: Invalid detailed Texel arguments\n";
        return 0;
    }

    for (index = 0; index < DETAILED_PARAMETER_COUNT; ++index) {
        base_values[index] = parameter_value(
            &base_params,
            DETAILED_PARAMETERS[index]
        );
        values[index] = base_values[index];
        {
            int radius = safe_parameter_radius(
                DETAILED_PARAMETERS[index],
                (int)base_values[index]
            );

            minimums[index] = DETAILED_PARAMETERS[index].minimum;
            maximums[index] = DETAILED_PARAMETERS[index].maximum;
            if (minimums[index] < base_values[index] - radius) {
                minimums[index] = (int)base_values[index] - radius;
            }
            if (maximums[index] > base_values[index] + radius) {
                maximums[index] = (int)base_values[index] + radius;
            }
        }
    }

    if (!load_detailed_dataset(dataset_path, base_params, &samples)) {
        return 0;
    }

    report_interval = iterations / 10;
    if (report_interval < 1) {
        report_interval = 1;
    }

    std::cout << "detailed texel samples " << samples.size()
              << " parameters " << DETAILED_PARAMETER_COUNT
              << " initialLoss " << detailed_loss(
                  samples, values, base_values
              ) << "\n";

    for (iteration = 1; iteration <= iterations; ++iteration) {
        std::vector<double> gradients(DETAILED_PARAMETER_COUNT, 0.0);
        size_t sample_index;

        for (sample_index = 0; sample_index < samples.size();
             ++sample_index) {
            double score = linear_score(
                samples[sample_index], values, base_values
            );
            double probability = texel_probability(score);
            double error = probability - samples[sample_index].result;
            double slope = probability * (1.0 - probability) / 400.0;

            for (index = 0; index < DETAILED_PARAMETER_COUNT; ++index) {
                gradients[index] += 2.0 * error * slope *
                    samples[sample_index].sensitivity[index];
            }
        }

        for (index = 0; index < DETAILED_PARAMETER_COUNT; ++index) {
            values[index] -= learning_rate * gradients[index] /
                (double)samples.size();
        }
        clamp_values(&values, minimums, maximums);

        if (iteration % report_interval == 0 || iteration == iterations) {
            std::cout << "detailed texel iteration " << iteration
                      << " loss " << detailed_loss(
                          samples, values, base_values
                      ) << "\n";
        }
    }

    std::cout << "detailed texel parameters";
    for (index = 0; index < DETAILED_PARAMETER_COUNT; ++index) {
        int value = values[index] >= 0.0
            ? (int)(values[index] + 0.5)
            : (int)(values[index] - 0.5);

        std::cout << " " << DETAILED_PARAMETERS[index].config_key
                  << "=" << value;
    }
    std::cout << "\n";
    return write_detailed_config(output_path, values);
}
