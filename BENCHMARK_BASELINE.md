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
| 2 | 5 | -345 | d5d6 | 21,045 |
| 3 | 5 | -31 | a5a6 | 748 |
| 4 | 5 | 0 | e1f2 | 6,525 |
| **Total** | | | | **32,175** |

## Current strength measurements

Measured 2026-08-16 with Cute Chess, 100 games, paired colors, sequential
openings, `2+0.02`, 16 MB hash, one thread, and Stockfish 18 limited to the
corresponding target Elo:

| Mode | Stockfish limit | Score | Elo difference | Estimated performance |
| --- | ---: | ---: | ---: | ---: |
| NNUE enabled | 2800 | 43-16-41 | +96.2 +/- 53.2 | ~2896 |
| NNUE disabled | 2500 | 39-42-19 | -10.4 +/- 61.9 | ~2490 |

## Current NPS measurements

The deterministic `bench` command above runs with NNUE disabled and searched
32,175 nodes in the latest Windows native build. Five repeated runs measured
170,238-185,982 NPS (average ~179.5k). For a directly comparable fixed-time
measurement, three 1-second start-position searches produced:

| Mode | Runs | Average NPS |
| --- | --- | ---: |
| NNUE disabled | 182,373; 181,590; 172,931 | ~179.0k |
| NNUE enabled | 105,116; 100,006; 103,800 | ~103.0k |
