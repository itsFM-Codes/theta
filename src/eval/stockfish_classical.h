#ifndef STOCKFISH_CLASSICAL_H
#define STOCKFISH_CLASSICAL_H

#include "src/chess/position.h"

struct ClassicalEvalState;

typedef struct StockfishClassicalBreakdown {
    int piece;
    int mobility;
    int king;
    int threats;
    int passed;
    int space;
    int total;
} StockfishClassicalBreakdown;

/*
 * A compact, attack-aware classical term set inspired by the Stockfish 11
 * handcrafted evaluator.  The result is from White's point of view and is
 * scaled by EvalParams::stockfish_classical_scale, or by the independent
 * per-component scales when those are configured.
 */
int stockfish_classical_score(
    const Position *position,
    const struct ClassicalEvalState *state
);
int stockfish_classical_score_with_breakdown(
    const Position *position,
    const struct ClassicalEvalState *state,
    StockfishClassicalBreakdown *breakdown
);

#endif // STOCKFISH_CLASSICAL_H
