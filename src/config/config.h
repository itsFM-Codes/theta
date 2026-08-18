#ifndef CONFIG_H
#define CONFIG_H

typedef struct EngineConfig {
    int max_depth;
    int allow_draws;
    int threads;
    int search_lmr_depth_start;
    int search_lmr_move_start;
    int search_null_move_base;
    int search_static_futility_margin;
} EngineConfig;

extern EngineConfig g_config;

void set_default_config(EngineConfig *config);
int load_config(const char *filename);

#endif // CONFIG_H
