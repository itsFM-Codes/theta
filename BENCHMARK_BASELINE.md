# Theta benchmark baseline

Command: `build\\theta.exe bench`

The benchmark uses four fixed positions at depth 5. Node counts, scores, and
best moves are expected to be deterministic for a given release. Elapsed time
and NPS are recorded by the command but vary by machine and system load.

| Position | Depth | Score | Best move | Nodes |
| --- | ---: | ---: | --- | ---: |
| 1 | 5 | 45 | g1f3 | 4,702 |
| 2 | 5 | -384 | d5d6 | 12,877 |
| 3 | 5 | 5 | b5b6 | 1,026 |
| 4 | 5 | 0 | e2d2 | 15,477 |
| **Total** | | | | **34,082** |
