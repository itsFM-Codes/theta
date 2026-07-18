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
| 1 | 5 | 45 | b1c3 | 2,714 |
| 2 | 5 | -395 | e2a6 | 6,920 |
| 3 | 5 | 5 | b5b6 | 651 |
| 4 | 5 | 0 | e2e8 | 3,980 |
| **Total** | | | | **14,265** |
