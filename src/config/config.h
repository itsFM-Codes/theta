#ifndef CONFIG_H
#define CONFIG_H

typedef struct EngineConfig {
    int max_depth;
    int allow_draws;
} EngineConfig;

extern EngineConfig g_config;

void set_default_config(EngineConfig *config);
int load_config(const char *filename);

#endif // CONFIG_H
