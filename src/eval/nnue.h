#ifndef NNUE_H
#define NNUE_H

#include "src/chess/position.h"

#define THETA_NNUE_INPUT_FEATURES 780
#define THETA_NNUE_HIDDEN_SIZE 64
#define THETA_NNUE_OUTPUT_HIDDEN_SIZE 32
#define STOCKFISH_NNUE_HALF_DIMENSIONS 1024
#define STOCKFISH_NNUE_BUCKETS 8

struct Move;
struct UndoState;

typedef struct NnuePositionData {
    unsigned char pieces[64];
    uint64_t occupied;
    uint64_t colors[2];
    uint64_t types[8];
    int kings[2];
} NnuePositionData;

typedef struct NnueState {
    int16_t psq_accumulators[2][STOCKFISH_NNUE_HALF_DIMENSIONS];
    int32_t psqt_accumulators[2][STOCKFISH_NNUE_BUCKETS];
    NnuePositionData position_data;
    unsigned int generation;
    int valid;
} NnueState;

int nnue_load(const char *path);
void nnue_unload(void);
void nnue_set_enabled(int enabled);
int nnue_is_enabled(void);
int nnue_is_loaded(void);
unsigned int nnue_generation(void);
int nnue_evaluate(const Position *position, int *score);
int nnue_state_build(const Position *position, NnueState *state);
int nnue_state_update(
    const Position *position,
    const Move *move,
    const UndoState *undo,
    const NnueState *parent,
    NnueState *child
);
int nnue_evaluate_with_state(
    const Position *position,
    const NnueState *state,
    int *score
);

#endif
