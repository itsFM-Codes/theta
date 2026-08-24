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
| 1 | 5 | 23 | b1c3 | 3,955 |
| 2 | 5 | -345 | d5d6 | 21,189 |
| 3 | 5 | -31 | a5a6 | 771 |
| 4 | 5 | 0 | e1f2 | 7,405 |
| **Total** | | | | **33,320** |

## Current controlled strength measurements

Measured 2026-08-23 with Cute Chess, paired colors, sequential
openings, `2+0.02`, 16 MB hash, one thread, and Stockfish 18 limited to the
corresponding target Elo:

| Mode | Stockfish limit | Score | Elo difference | Estimated performance |
| --- | ---: | ---: | ---: | ---: |
| NNUE enabled | 2800 | 47-18-35 (100 games) | +103.7 +/- 56.5 | ~2904 |
| NNUE disabled | 2500 | 25-59-16 (100 games) | -123.0 +/- 66.5 | ~2377 |

## Current NPS measurements

The deterministic `bench` command above runs with NNUE disabled and searched
33,320 nodes in the current Windows release build. The recorded run measured
169,137 NPS; repeated measurements vary with system load. The exact retained
candidate binary hash is
`4B2A7D890D42163F9E958F924C9266C0CB47EA38CDA6B47D511E0BD36E78B942`.

Earlier cleared-hash, single-threaded start-position `go nodes 500000`
samples produced:

| Mode | Runs | Average NPS |
| --- | --- | ---: |
| NNUE disabled | 173,429; 168,923; 169,300 | ~170.6k |
| NNUE enabled | 93,012; 93,153; 93,984 | ~93.4k |
