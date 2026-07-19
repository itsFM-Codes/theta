#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

EngineConfig g_config;

static char *trim(char *text) {
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
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

void set_default_config(EngineConfig *config) {
    config->max_depth = 6;
}

int load_config(const char *filename) {
    FILE *file = fopen(filename, "r");
    char line[256];
    int line_number = 0;

    set_default_config(&g_config);

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

        if (!parse_int(value, &number)) {
            fprintf(stderr, "Warning: Invalid value for %s on line %d\n", key, line_number);
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
    return 1;
}
