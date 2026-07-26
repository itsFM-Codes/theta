#include "eval_features.h"

#include "king_safety.h"
#include "mobility.h"
#include "pawn_structure.h"
#include "piece_activity.h"
#include "piece_square_tables.h"
#include "strategic.h"

static const EvalFeatureDefinition FEATURE_DEFINITIONS[EVAL_FEATURE_COUNT] = {
    {"material", "eval_material_scale", 256, 224, 288},
    {"pieceSquare", "eval_piece_square_scale", 256, 0, 512},
    {"mobility", "eval_mobility_scale", 256, 0, 512},
    {"pawnStructure", "eval_pawn_structure_scale", 256, 0, 512},
    {"kingSafety", "eval_king_safety_scale", 256, 0, 512},
    {"pieceActivity", "eval_piece_activity_scale", 256, 0, 512},
    {"threats", "eval_threat_scale", 256, 0, 512},
    {"space", "eval_space_scale", 256, 0, 512},
    {"tempo", "eval_tempo_bonus", 0, -50, 50}
};

static int pop_first_square(uint64_t *squares) {
    uint64_t value = *squares;
    int square;

    if (value == 0) {
        return NO_SQUARE;
    }

    square = __builtin_ctzll(value);
    *squares = value & (value - 1);
    return square;
}

static int piece_phase(Piece piece) {
    switch (piece_type(piece)) {
        case PIECE_TYPE_KNIGHT:
        case PIECE_TYPE_BISHOP:
            return 1;
        case PIECE_TYPE_ROOK:
            return 2;
        case PIECE_TYPE_QUEEN:
            return 4;
        default:
            return 0;
    }
}

static int endgame_weight_for_position(const Position *position) {
    const EvalParams *params = current_eval_params();
    uint64_t pieces;
    int phase = 0;

    if (position == 0 || params->max_phase <= 0) {
        return 0;
    }

    pieces = position->occupied;
    while (pieces != 0) {
        int square = pop_first_square(&pieces);

        phase += piece_phase(position_piece_at(position, square));
    }

    if (phase > params->max_phase) {
        phase = params->max_phase;
    }
    return (params->max_phase - phase) * 256 / params->max_phase;
}

const EvalFeatureDefinition *eval_feature_definition(int index) {
    if (index < 0 || index >= EVAL_FEATURE_COUNT) {
        return 0;
    }
    return &FEATURE_DEFINITIONS[index];
}

void extract_eval_features(
    const Position *position,
    EvalFeatureVector *features
) {
    uint64_t pieces;
    int endgame_weight;
    int index;

    if (features == 0) {
        return;
    }

    for (index = 0; index < EVAL_FEATURE_COUNT; ++index) {
        features->values[index] = 0.0;
    }

    if (position == 0) {
        return;
    }

    endgame_weight = endgame_weight_for_position(position);
    pieces = position->occupied;
    while (pieces != 0) {
        int square = pop_first_square(&pieces);
        Piece piece = position_piece_at(position, square);
        int sign = piece_color(piece) == COLOR_WHITE ? 1 : -1;

        if (piece_color(piece) == COLOR_NONE) {
            continue;
        }

        features->values[EVAL_FEATURE_MATERIAL] +=
            sign * eval_piece_type_value(piece_type(piece));
        features->values[EVAL_FEATURE_PIECE_SQUARE] +=
            sign * piece_square_value(piece, square, endgame_weight);
    }

    features->values[EVAL_FEATURE_MOBILITY] = mobility_score(position);
    features->values[EVAL_FEATURE_PAWN_STRUCTURE] =
        pawn_structure_score(position);
    features->values[EVAL_FEATURE_KING_SAFETY] =
        king_safety_score(position, endgame_weight);
    features->values[EVAL_FEATURE_PIECE_ACTIVITY] =
        piece_activity_score(position, endgame_weight);
    features->values[EVAL_FEATURE_THREATS] = threat_score(position);
    features->values[EVAL_FEATURE_SPACE] = space_score(position);
    features->values[EVAL_FEATURE_TEMPO] =
        position->side_to_move == COLOR_WHITE ? 256.0 : -256.0;
}

double eval_features_score(
    const EvalFeatureVector *features,
    const double weights[EVAL_FEATURE_COUNT]
) {
    double score = 0.0;
    int index;

    if (features == 0 || weights == 0) {
        return 0.0;
    }

    for (index = 0; index < EVAL_FEATURE_COUNT; ++index) {
        score += features->values[index] * weights[index] / 256.0;
    }
    return score;
}

void eval_feature_weights_from_params(
    const EvalParams *params,
    double weights[EVAL_FEATURE_COUNT]
) {
    if (params == 0 || weights == 0) {
        return;
    }

    weights[EVAL_FEATURE_MATERIAL] = params->material_scale;
    weights[EVAL_FEATURE_PIECE_SQUARE] = params->piece_square_scale;
    weights[EVAL_FEATURE_MOBILITY] = params->mobility_scale;
    weights[EVAL_FEATURE_PAWN_STRUCTURE] = params->pawn_structure_scale;
    weights[EVAL_FEATURE_KING_SAFETY] = params->king_safety_scale;
    weights[EVAL_FEATURE_PIECE_ACTIVITY] = params->piece_activity_scale;
    weights[EVAL_FEATURE_THREATS] = params->threat_scale;
    weights[EVAL_FEATURE_SPACE] = params->space_scale;
    weights[EVAL_FEATURE_TEMPO] = params->tempo_bonus;
}

static int rounded_weight(double value) {
    return value >= 0.0
        ? (int)(value + 0.5)
        : (int)(value - 0.5);
}

void eval_params_from_feature_weights(
    EvalParams *params,
    const double weights[EVAL_FEATURE_COUNT]
) {
    if (params == 0 || weights == 0) {
        return;
    }

    params->material_scale = rounded_weight(weights[EVAL_FEATURE_MATERIAL]);
    params->piece_square_scale =
        rounded_weight(weights[EVAL_FEATURE_PIECE_SQUARE]);
    params->mobility_scale = rounded_weight(weights[EVAL_FEATURE_MOBILITY]);
    params->pawn_structure_scale =
        rounded_weight(weights[EVAL_FEATURE_PAWN_STRUCTURE]);
    params->king_safety_scale =
        rounded_weight(weights[EVAL_FEATURE_KING_SAFETY]);
    params->piece_activity_scale =
        rounded_weight(weights[EVAL_FEATURE_PIECE_ACTIVITY]);
    params->threat_scale = rounded_weight(weights[EVAL_FEATURE_THREATS]);
    params->space_scale = rounded_weight(weights[EVAL_FEATURE_SPACE]);
    params->tempo_bonus = rounded_weight(weights[EVAL_FEATURE_TEMPO]);
}
