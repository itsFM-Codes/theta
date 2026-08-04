#ifndef NNUE_H
#define NNUE_H

#include "src/chess/position.h"

#define THETA_NNUE_INPUT_FEATURES 780
#define THETA_NNUE_HIDDEN_SIZE 64
#define THETA_NNUE_OUTPUT_HIDDEN_SIZE 32

int nnue_load(const char *path);
void nnue_unload(void);
void nnue_set_enabled(int enabled);
int nnue_is_enabled(void);
int nnue_is_loaded(void);
unsigned int nnue_generation(void);
int nnue_evaluate(const Position *position, int *score);

#endif
