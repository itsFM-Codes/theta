#ifndef EVAL_TUNING_H
#define EVAL_TUNING_H

int print_eval_features_json(const char *fen);
int print_eval_params_json(void);
int run_texel_tuning(
    const char *dataset_path,
    int iterations,
    double learning_rate,
    const char *output_path
);

#endif // EVAL_TUNING_H
