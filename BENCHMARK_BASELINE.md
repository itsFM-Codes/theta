# Theta benchmark baseline

Command: `build\\theta.exe bench`

The benchmark uses four fixed positions at depth 5. Node counts, scores, and
best moves are expected to be deterministic for a given release. Elapsed time
and NPS are recorded by the command but vary by machine and system load.

| Position | Depth | Score | Best move | Nodes |
| --- | ---: | ---: | --- | ---: |
| 1 | 5 | 13 | b1c3 | 5,886 |
| 2 | 5 | -384 | d5d6 | 13,048 |
| 3 | 5 | 5 | b5b6 | 997 |
| 4 | 5 | 0 | e2e3 | 10,814 |
| **Total** | | | | **30,745** |

