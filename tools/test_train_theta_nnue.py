"""Regression tests for the native NNUE data-perspective contract."""

from pathlib import Path
import sys
import tempfile

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from train_theta_nnue import load_rows  # noqa: E402


def main() -> None:
    # The same board with the side to move changed should receive opposite
    # side-to-move targets when the source probabilities are White-relative.
    board = "8/8/8/8/8/8/4k3/4K3"
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".dataset", delete=False, encoding="utf-8"
    ) as handle:
        path = Path(handle.name)
        handle.write(f"0.8807970778 {board} w - - 0 1\n")
        handle.write(f"0.8807970778 {board} b - - 0 1\n")

    try:
        _, white_targets = load_rows([path], "white")
        _, side_targets = load_rows([path], "side-to-move")
    finally:
        path.unlink()

    assert white_targets.shape == (2,)
    assert white_targets[0] > 0.0
    assert white_targets[1] < 0.0
    assert np.isclose(white_targets[0], -white_targets[1])
    assert np.isclose(side_targets[0], side_targets[1])
    print("NNUE label perspective test passed")


if __name__ == "__main__":
    main()
