# Theta benchmark baseline

Command: `build\\theta.exe bench`

The benchmark uses four fixed positions at depth 5. Node counts, scores, and
best moves are expected to be deterministic for a given release.

How to read this table:

- `Depth` is how far the engine searched. At the same time control, higher
  depth is usually better because the engine is seeing farther.
- `Score` is the engine's evaluation from the side to move, in centipawns. A
  positive score favors the side to move; a negative score favors the opponent.
- `Best move` is the move the engine currently believes is best at this depth.
  A different best move is not automatically wrong, but it should be checked
  against tactics and, later, match results.
- `Nodes` is how many positions the engine searched. At the same fixed depth,
  lower nodes are usually better if score, best move, and tactical results do
  not regress, because the engine got to the same depth with less work.
- For fixed-depth benchmarks like this one, node count is mainly a deterministic
  efficiency check. For real playing strength, fixed-time depth, tactical
  correctness, move quality, and match results matter more.

Expected result: after a search-strength change, keep the change if tests pass,
the tactical suite still passes, the benchmark is deterministic across repeated
runs, and the new scores/best moves look sensible. Lower nodes at the same depth
are a good sign, but they are not proof of playing strength by themselves.

| Position | Depth | Score | Best move | Nodes |
| --- | ---: | ---: | --- | ---: |
| 1 | 5 | 39 | g1f3 | 3,857 |
| 2 | 5 | -345 | d5d6 | 21,172 |
| 3 | 5 | -31 | a5a6 | 748 |
| 4 | 5 | 0 | e1f2 | 6,525 |
| **Total** | | | | **32,302** |

## Current strength measurements

Measured 2026-08-05 with Cute Chess, 40 games, paired colors, sequential
openings, `2+0.02`, 16 MB hash, one thread, and Stockfish 18 limited to Elo
2800:

| Mode | Score | Elo difference | Estimated performance |
| --- | ---: | ---: | ---: |
| NNUE enabled | 13-14-13 | -8.7 +/- 90.3 | ~2791 |
| NNUE disabled | 3-33-4 | -338.0 +/- 175.3 | ~2462 |

These are provisional 40-game estimates; the confidence intervals are wide.

## Current NPS measurements

The deterministic `bench` command above runs with NNUE disabled and measured
169,120 NPS on the current Windows native build. For a directly comparable
fixed-time measurement, three 1-second start-position searches produced:

| Mode | Runs | Average NPS |
| --- | --- | ---: |
| NNUE disabled | 138,567; 147,805; 141,450 | ~142.6k |
| NNUE enabled | 43,864; 44,127; 47,670 | ~45.2k |
