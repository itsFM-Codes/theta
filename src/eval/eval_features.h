#ifndef EVAL_FEATURES_H
#define EVAL_FEATURES_H

#include "eval_params.h"
#include "src/chess/position.h"

#define EVAL_FEATURE_COUNT 9

typedef enum EvalFeatureIndex {
    EVAL_FEATURE_MATERIAL,
    EVAL_FEATURE_PIECE_SQUARE,
    EVAL_FEATURE_MOBILITY,
    EVAL_FEATURE_PAWN_STRUCTURE,
    EVAL_FEATURE_KING_SAFETY,
    EVAL_FEATURE_PIECE_ACTIVITY,
    EVAL_FEATURE_THREATS,
    EVAL_FEATURE_SPACE,
    EVAL_FEATURE_TEMPO
} EvalFeatureIndex;

typedef struct EvalFeatureDefinition {
    const char *name;
    const char *config_key;
    int default_weight;
    int minimum_weight;
    int maximum_weight;
} EvalFeatureDefinition;

typedef struct EvalFeatureVector {
    double values[EVAL_FEATURE_COUNT];
} EvalFeatureVector;

const EvalFeatureDefinition *eval_feature_definition(int index);
void extract_eval_features(
    const Position *position,
    EvalFeatureVector *features
);
double eval_features_score(
    const EvalFeatureVector *features,
    const double weights[EVAL_FEATURE_COUNT]
);
void eval_feature_weights_from_params(
    const EvalParams *params,
    double weights[EVAL_FEATURE_COUNT]
);
void eval_params_from_feature_weights(
    EvalParams *params,
    const double weights[EVAL_FEATURE_COUNT]
);

#endif // EVAL_FEATURES_H
