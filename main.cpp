#include <stdio.h>
#include <stdlib.h>
#include "src/config/config.h"

// Config File
#define CONFIG_FILE "config/config.conf"

int main() {
    if (!load_config(CONFIG_FILE)) {
        return EXIT_FAILURE;
    }

    return 0;
}
