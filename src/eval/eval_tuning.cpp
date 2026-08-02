#include "eval_tuning.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "eval_features.h"
#include "src/chess/fen.h"

typedef struct TexelSample {
    EvalFeatureVector features;
    double result;
} TexelSample;

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

    {
        char *end = 0;
        double value = strtod(text.c_str(), &end);

        if (end != text.c_str() && *end == '\0' &&
            value >= 0.0 && value <= 1.0) {
            *result = value;
            return 1;
        }
    }

    return 0;
}

static int load_texel_dataset(
    const char *dataset_path,
    std::vector<TexelSample> *samples
) {
    std::ifstream file(dataset_path);
    std::string line;
    int line_number = 0;

    if (!file.is_open() || samples == 0) {
        std::cerr << "Error: Could not open dataset " << dataset_path << "\n";
        return 0;
    }

    while (std::getline(file, line)) {
        std::string trimmed = trim_string(line);
        size_t separator;
        std::string result_text;
        std::string fen;
        double result;
        Position position;
        TexelSample sample = {};

        line_number++;
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        separator = trimmed.find_first_of(" \t");
        if (separator == std::string::npos) {
            std::cerr << "Warning: Invalid dataset line "
                      << line_number << "\n";
            continue;
        }

        result_text = trimmed.substr(0, separator);
        fen = trim_string(trimmed.substr(separator + 1));
        if (!parse_result_token(result_text, &result) ||
            !position_from_fen(&position, fen.c_str())) {
            std::cerr << "Warning: Skipped dataset line "
                      << line_number << "\n";
            continue;
        }

        sample.result = result;
        extract_eval_features(&position, &sample.features);
        samples->push_back(sample);
    }

    if (samples->empty()) {
        std::cerr << "Error: Dataset has no usable positions\n";
        return 0;
    }

    return 1;
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

static void clamp_weights(double weights[EVAL_FEATURE_COUNT]) {
    int index;

    for (index = 0; index < EVAL_FEATURE_COUNT; ++index) {
        const EvalFeatureDefinition *definition =
            eval_feature_definition(index);

        if (definition == 0) {
            continue;
        }
        if (weights[index] < definition->minimum_weight) {
            weights[index] = definition->minimum_weight;
        }
        if (weights[index] > definition->maximum_weight) {
            weights[index] = definition->maximum_weight;
        }
    }
}

static double texel_loss(
    const std::vector<TexelSample> &samples,
    const double weights[EVAL_FEATURE_COUNT]
) {
    double loss = 0.0;
    size_t index;

    for (index = 0; index < samples.size(); ++index) {
        double score = eval_features_score(&samples[index].features, weights);
        double probability = texel_probability(score);
        double error = probability - samples[index].result;

        loss += error * error;
    }
    return loss / samples.size();
}

static int write_weight_config(
    const char *output_path,
    const double weights[EVAL_FEATURE_COUNT]
) {
    std::ofstream output(output_path);
    int index;

    if (!output.is_open()) {
        std::cerr << "Error: Could not write " << output_path << "\n";
        return 0;
    }

    output << "# Theta Texel tuning output\n";
    output << "# Paste these keys into config/config.conf\n";
    for (index = 0; index < EVAL_FEATURE_COUNT; ++index) {
        const EvalFeatureDefinition *definition =
            eval_feature_definition(index);
        int value = weights[index] >= 0.0
            ? (int)(weights[index] + 0.5)
            : (int)(weights[index] - 0.5);

        if (definition == 0) {
            continue;
        }
        output << definition->config_key << " = " << value << "\n";
    }
    return 1;
}

int print_eval_features_json(const char *fen) {
    Position position;
    EvalFeatureVector features = {};
    double weights[EVAL_FEATURE_COUNT];
    double score;
    int index;

    if (!position_from_fen(&position, fen)) {
        std::cerr << "Error: Invalid FEN\n";
        return 0;
    }

    extract_eval_features(&position, &features);
    eval_feature_weights_from_params(current_eval_params(), weights);
    score = eval_features_score(&features, weights);

    std::cout << "{\"whiteScore\":" << (int)(score >= 0.0
              ? score + 0.5
              : score - 0.5)
              << ",\"features\":[";
    for (index = 0; index < EVAL_FEATURE_COUNT; ++index) {
        const EvalFeatureDefinition *definition =
            eval_feature_definition(index);

        if (index > 0) {
            std::cout << ",";
        }
        std::cout << "{\"name\":\"" << definition->name
                  << "\",\"value\":" << features.values[index] << "}";
    }
    std::cout << "]}\n";
    return 1;
}

int print_eval_params_json(void) {
    const EvalParams *params = current_eval_params();
    double weights[EVAL_FEATURE_COUNT];
    int index;

    eval_feature_weights_from_params(params, weights);
    std::cout << "{\"features\":[";
    for (index = 0; index < EVAL_FEATURE_COUNT; ++index) {
        const EvalFeatureDefinition *definition =
            eval_feature_definition(index);

        if (index > 0) {
            std::cout << ",";
        }
        std::cout << "{\"name\":\"" << definition->name
                  << "\",\"config\":\"" << definition->config_key
                  << "\",\"weight\":" << weights[index] << "}";
    }
    std::cout << "]}\n";
    return 1;
}

int run_texel_tuning(
    const char *dataset_path,
    int iterations,
    double learning_rate,
    const char *output_path
) {
    std::vector<TexelSample> samples;
    double weights[EVAL_FEATURE_COUNT];
    int iteration;
    int report_interval;
    int index;

    if (dataset_path == 0 || output_path == 0 ||
        iterations <= 0 || learning_rate <= 0.0) {
        std::cerr << "Error: Invalid Texel arguments\n";
        return 0;
    }

    if (!load_texel_dataset(dataset_path, &samples)) {
        return 0;
    }

    eval_feature_weights_from_params(current_eval_params(), weights);
    report_interval = iterations / 10;
    if (report_interval < 1) {
        report_interval = 1;
    }

    std::cout << "texel samples " << samples.size()
              << " initialLoss " << texel_loss(samples, weights) << "\n";

    for (iteration = 1; iteration <= iterations; ++iteration) {
        double gradients[EVAL_FEATURE_COUNT] = {};
        size_t sample_index;

        for (sample_index = 0; sample_index < samples.size(); ++sample_index) {
            double score = eval_features_score(
                &samples[sample_index].features,
                weights
            );
            double probability = texel_probability(score);
            double error = probability - samples[sample_index].result;
            double slope = probability * (1.0 - probability) / 400.0;

            for (index = 0; index < EVAL_FEATURE_COUNT; ++index) {
                gradients[index] +=
                    2.0 * error * slope *
                    samples[sample_index].features.values[index] / 256.0;
            }
        }

        for (index = 0; index < EVAL_FEATURE_COUNT; ++index) {
            weights[index] -= learning_rate * gradients[index] /
                              (double)samples.size();
        }
        clamp_weights(weights);

        if (iteration % report_interval == 0 || iteration == iterations) {
            std::cout << "texel iteration " << iteration
                      << " loss " << texel_loss(samples, weights) << "\n";
        }
    }

    std::cout << "texel weights";
    for (index = 0; index < EVAL_FEATURE_COUNT; ++index) {
        const EvalFeatureDefinition *definition =
            eval_feature_definition(index);
        int value = weights[index] >= 0.0
            ? (int)(weights[index] + 0.5)
            : (int)(weights[index] - 0.5);

        std::cout << " " << definition->config_key << "=" << value;
    }
    std::cout << "\n";

    return write_weight_config(output_path, weights);
}
