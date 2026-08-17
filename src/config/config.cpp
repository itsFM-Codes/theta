#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include "src/eval/eval_params.h"

EngineConfig g_config = {6, 1, 3, 3, 4, 105};

typedef struct ConfigIntTarget {
    const char *key;
    int *value;
    int minimum;
    int maximum;
} ConfigIntTarget;

static char *trim(char *text) {
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end -= 1;
    }
    *end = '\0';

    return text;
}

static int parse_int(const char *text, int *value) {
    char *end;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *trim(end) != '\0' ||
        parsed < INT_MIN || parsed > INT_MAX) {
        return 0;
    }

    *value = (int)parsed;
    return 1;
}

static int text_equals_ignore_case(const char *first, const char *second) {
    while (*first != '\0' && *second != '\0') {
        if (tolower((unsigned char)*first) !=
            tolower((unsigned char)*second)) {
            return 0;
        }
        first++;
        second++;
    }
    return *first == '\0' && *second == '\0';
}

static int parse_bool(const char *text, int *value) {
    if (text_equals_ignore_case(text, "true") || strcmp(text, "1") == 0) {
        *value = 1;
        return 1;
    }
    if (text_equals_ignore_case(text, "false") || strcmp(text, "0") == 0) {
        *value = 0;
        return 1;
    }
    return 0;
}

static int set_config_int_target(
    const ConfigIntTarget *targets,
    int count,
    const char *key,
    int number,
    int line_number
) {
    int index;

    for (index = 0; index < count; ++index) {
        if (strcmp(key, targets[index].key) != 0) {
            continue;
        }
        if (number < targets[index].minimum ||
            number > targets[index].maximum) {
            fprintf(
                stderr,
                "Warning: %s must be between %d and %d on line %d\n",
                key,
                targets[index].minimum,
                targets[index].maximum,
                line_number
            );
            return 1;
        }
        *targets[index].value = number;
        return 1;
    }
    return 0;
}

void set_default_config(EngineConfig *config) {
    config->max_depth = 6;
    config->allow_draws = 1;
    config->search_lmr_depth_start = 3;
    config->search_lmr_move_start = 3;
    config->search_null_move_base = 4;
    config->search_static_futility_margin = 105;
}

int load_config(const char *filename) {
    FILE *file = fopen(filename, "r");
    EvalParams eval_params;
    char line[256];
    int line_number = 0;

    set_default_config(&g_config);
    reset_current_eval_params();
    eval_params = *current_eval_params();

    if (file == NULL) {
        fprintf(stderr, "Error: Could not open config file %s\n", filename);
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *key;
        char *value;
        char *equals;
        char *comment;
        int number;
        ConfigIntTarget eval_targets[] = {
            {"eval_material_scale", &eval_params.material_scale, 224, 288},
            {"eval_piece_square_scale", &eval_params.piece_square_scale, 0, 512},
            {"eval_mobility_scale", &eval_params.mobility_scale, 0, 512},
            {"eval_pawn_structure_scale", &eval_params.pawn_structure_scale, 0, 512},
            {"eval_king_safety_scale", &eval_params.king_safety_scale, 0, 512},
            {"eval_piece_activity_scale", &eval_params.piece_activity_scale, 0, 512},
            {"eval_threat_scale", &eval_params.threat_scale, 0, 512},
            {"eval_space_scale", &eval_params.space_scale, 0, 512},
            {"eval_tempo_bonus", &eval_params.tempo_bonus, -50, 50},
            {"eval_pawn_value", &eval_params.piece_values[PIECE_TYPE_PAWN], 50, 200},
            {"eval_knight_value", &eval_params.piece_values[PIECE_TYPE_KNIGHT], 200, 450},
            {"eval_bishop_value", &eval_params.piece_values[PIECE_TYPE_BISHOP], 200, 450},
            {"eval_rook_value", &eval_params.piece_values[PIECE_TYPE_ROOK], 350, 700},
            {"eval_queen_value", &eval_params.piece_values[PIECE_TYPE_QUEEN], 700, 1200},
            {"eval_knight_mobility_weight", &eval_params.knight_mobility_weight, 0, 8},
            {"eval_bishop_mobility_weight", &eval_params.bishop_mobility_weight, 0, 8},
            {"eval_rook_mobility_weight", &eval_params.rook_mobility_weight, 0, 8},
            {"eval_queen_mobility_weight", &eval_params.queen_mobility_weight, 0, 8},
            {"eval_doubled_pawn_penalty", &eval_params.doubled_pawn_penalty, 0, 50},
            {"eval_isolated_pawn_penalty", &eval_params.isolated_pawn_penalty, 0, 50},
            {"eval_pawn_island_penalty", &eval_params.pawn_island_penalty, 0, 50},
            {"eval_backward_pawn_penalty", &eval_params.backward_pawn_penalty, 0, 50},
            {"eval_candidate_passed_pawn_bonus", &eval_params.candidate_passed_pawn_bonus, 0, 80},
            {"eval_supported_passed_pawn_bonus", &eval_params.supported_passed_pawn_bonus, 0, 80},
            {"eval_connected_passed_pawn_bonus", &eval_params.connected_passed_pawn_bonus, 0, 80},
            {"eval_blocked_passed_pawn_penalty", &eval_params.blocked_passed_pawn_penalty, 0, 80},
            {"eval_passed_pawn_king_distance_scale", &eval_params.passed_pawn_king_distance_scale, 0, 10},
            {"eval_pawn_shield_bonus", &eval_params.pawn_shield_bonus, 0, 50},
            {"eval_semi_open_file_penalty", &eval_params.semi_open_file_penalty, 0, 50},
            {"eval_open_file_penalty", &eval_params.open_file_penalty, 0, 50},
            {"eval_king_ring_attack_unit", &eval_params.king_ring_attack_unit, 0, 20},
            {"eval_king_danger_quadratic_divisor", &eval_params.king_danger_quadratic_divisor, 1, 100},
            {"eval_max_king_danger", &eval_params.max_king_danger, 0, 400},
            {"eval_rook_file_pressure", &eval_params.rook_file_pressure, 0, 50},
            {"eval_queen_file_pressure", &eval_params.queen_file_pressure, 0, 80},
            {"eval_bishop_pair_middlegame_bonus", &eval_params.bishop_pair_middlegame_bonus, 0, 100},
            {"eval_bishop_pair_endgame_bonus", &eval_params.bishop_pair_endgame_bonus, 0, 120},
            {"eval_semi_open_rook_bonus", &eval_params.semi_open_rook_bonus, 0, 80},
            {"eval_open_rook_bonus", &eval_params.open_rook_bonus, 0, 80},
            {"eval_rook_seventh_rank_bonus", &eval_params.rook_seventh_rank_bonus, 0, 80},
            {"eval_knight_outpost_bonus", &eval_params.knight_outpost_bonus, 0, 80},
            {"eval_bad_bishop_pawn_penalty", &eval_params.bad_bishop_pawn_penalty, 0, 30},
            {"eval_bad_bishop_mobility_penalty", &eval_params.bad_bishop_mobility_penalty, 0, 60},
            {"eval_trapped_minor_penalty", &eval_params.trapped_minor_penalty, 0, 80},
            {"eval_pawn_threat_base", &eval_params.pawn_threat_base, 0, 80},
            {"eval_hanging_piece_divisor", &eval_params.hanging_piece_divisor, 1, 100},
            {"eval_safe_space_bonus", &eval_params.safe_space_bonus, 0, 20}
        };
        ConfigIntTarget search_targets[] = {
            {"search_lmr_depth_start", &g_config.search_lmr_depth_start, 2, 6},
            {"search_lmr_move_start", &g_config.search_lmr_move_start, 2, 8},
            {"search_null_move_base", &g_config.search_null_move_base, 1, 8},
            {"search_static_futility_margin", &g_config.search_static_futility_margin, 40, 200}
        };

        line_number++;
        comment = strpbrk(line, "#;");
        if (comment != NULL) {
            *comment = '\0';
        }

        key = trim(line);
        if (*key == '\0' || *key == '[') {
            continue;
        }

        equals = strchr(key, '=');
        if (equals == NULL) {
            fprintf(stderr, "Warning: Invalid config line %d\n", line_number);
            continue;
        }

        *equals = '\0';
        key = trim(key);
        value = trim(equals + 1);

        if (strcmp(key, "allow_draw") == 0 ||
            strcmp(key, "allow_draws") == 0) {
            if (!parse_bool(value, &g_config.allow_draws)) {
                fprintf(
                    stderr,
                    "Warning: %s must be true or false on line %d\n",
                    key,
                    line_number
                );
            }
            continue;
        }

        if (!parse_int(value, &number)) {
            fprintf(stderr, "Warning: Invalid value for %s on line %d\n", key, line_number);
            continue;
        }

        if (set_config_int_target(
                eval_targets,
                (int)(sizeof(eval_targets) / sizeof(eval_targets[0])),
                key,
                number,
                line_number
        )) {
            continue;
        }

        if (set_config_int_target(
                search_targets,
                (int)(sizeof(search_targets) / sizeof(search_targets[0])),
                key,
                number,
                line_number
            )) {
            continue;
        }

        if (strcmp(key, "threads") == 0) {
            fprintf(
                stderr,
                "Warning: threads is reserved for future multi-threaded search; using one thread\n"
            );
        } else if (strcmp(key, "max_depth") == 0) {
            if (number < 1 || number > 128) {
                fprintf(stderr, "Warning: max_depth must be between 1 and 128\n");
            } else {
                g_config.max_depth = number;
            }
        } else {
            fprintf(stderr, "Warning: Unknown config key %s on line %d\n", key, line_number);
        }
    }

    fclose(file);
    set_current_eval_params(&eval_params);
    return 1;
}
