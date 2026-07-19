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
| 1 | 5 | 52 | d2d4 | 5,123 |
| 2 | 5 | -372 | e2a6 | 4,717 |
| 3 | 5 | 5 | b5b6 | 648 |
| 4 | 5 | 0 | e1f2 | 6,609 |
| **Total** | | | | **17,097** |

This baseline was updated after adding quiescence transposition-table support
on 2026-07-19. The node count was reproduced in consecutive release runs, the
tactical suite passed 2/2, and the new engine scored 12 wins, 8 losses, and 8
draws against the preserved pre-change engine in a paired `2+0.02` match.

Elo (based on cutechess with stockfish-18): 1445

The baseline was updated again after the combined search-throughput milestone:
release build modes, direct pseudo-legal search move handling, lazy move
picking, clustered persistent hash storage, a pawn hash, continuation history,
ProbCut, and conservative singular extensions. The 17,097-node result was
reproduced in consecutive release runs and the tactical suite passed 2/2. In a
28-game paired `1+0.01` match against the preserved optimized pre-change
engine, the result was 10 wins, 10 losses, and 8 draws. This small sample is
neutral rather than evidence of a playing-strength gain.
