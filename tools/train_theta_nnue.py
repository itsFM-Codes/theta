"""Train and export Theta's compact THNNUE01 network.

The input files use Theta's existing ``probability fen`` format.  The feature
encoding is relative to the side to move, while the repository's generated
Texel/self-play files store probabilities from White's perspective.  The
loader therefore converts White-perspective labels to side-to-move labels by
default; use ``--label-perspective side-to-move`` for already-converted data.

The exported layout is the one consumed by src/eval/nnue.cpp:
780 sparse input features -> 64 -> 32 -> 1.
"""

from __future__ import annotations

import argparse
import math
import random
import struct
from pathlib import Path

import chess
import numpy as np
import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset


INPUT_FEATURES = 780
HIDDEN_SIZE = 64
OUTPUT_HIDDEN_SIZE = 32
OUTPUT_SCALE = 1000.0
SCORE_CLIP = 3000.0


def theta_square(square: chess.Square) -> int:
    return (7 - chess.square_rank(square)) * 8 + chess.square_file(square)


def feature_ids(board: chess.Board) -> list[int]:
    result: list[int] = []
    for square, piece in board.piece_map().items():
        relative_color = 0 if piece.color == board.turn else 1
        piece_type = piece.piece_type - 1
        result.append(
            (relative_color * 6 + piece_type) * 64 + theta_square(square)
        )

    rights = [
        (board.turn, board.has_kingside_castling_rights(board.turn)),
        (board.turn, board.has_queenside_castling_rights(board.turn)),
        (not board.turn, board.has_kingside_castling_rights(not board.turn)),
        (not board.turn, board.has_queenside_castling_rights(not board.turn)),
    ]
    result.extend(768 + index for index, (_, present) in enumerate(rights)
                  if present)
    if board.ep_square is not None:
        result.append(772 + chess.square_file(board.ep_square))
    return result


def probability_to_score(value: float) -> float:
    value = min(max(value, 0.0025), 0.9975)
    return 400.0 * math.log(value / (1.0 - value))


def load_rows(
    paths: list[Path],
    label_perspective: str = "white",
) -> tuple[np.ndarray, np.ndarray]:
    if label_perspective not in {"white", "side-to-move"}:
        raise ValueError(
            "label_perspective must be 'white' or 'side-to-move'"
        )

    rows: list[list[int]] = []
    targets: list[float] = []
    seen: set[str] = set()

    for path in paths:
        with path.open(encoding="utf-8") as handle:
            for line_number, raw_line in enumerate(handle, 1):
                line = raw_line.strip()
                if not line or line.startswith("#"):
                    continue
                fields = line.split(maxsplit=1)
                if len(fields) != 2:
                    continue
                try:
                    probability = float(fields[0])
                    board = chess.Board(fields[1])
                except (ValueError, chess.InvalidBoardException) as error:
                    raise ValueError(
                        f"invalid row in {path}:{line_number}"
                    ) from error
                fen = board.fen()
                if fen in seen:
                    continue
                seen.add(fen)
                rows.append(feature_ids(board))
                target_score = probability_to_score(probability)
                if label_perspective == "white" and board.turn == chess.BLACK:
                    target_score = -target_score
                targets.append(
                    min(max(target_score, -SCORE_CLIP), SCORE_CLIP)
                    / OUTPUT_SCALE
                )

    if not rows:
        raise ValueError("the training files contain no usable positions")

    features = np.zeros((len(rows), INPUT_FEATURES), dtype=np.float32)
    for index, active in enumerate(rows):
        features[index, active] = 1.0
    return features, np.asarray(targets, dtype=np.float32)


class ThetaNNUE(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.fc1 = nn.Linear(INPUT_FEATURES, HIDDEN_SIZE)
        self.fc2 = nn.Linear(HIDDEN_SIZE, OUTPUT_HIDDEN_SIZE)
        self.out = nn.Linear(OUTPUT_HIDDEN_SIZE, 1)

    def forward(self, features: torch.Tensor) -> torch.Tensor:
        hidden = torch.relu(self.fc1(features))
        hidden = torch.relu(self.fc2(hidden))
        return self.out(hidden).squeeze(-1)


def export_model(model: ThetaNNUE, output: Path) -> None:
    model = model.to("cpu").eval()
    with torch.no_grad():
        first_weights = model.fc1.weight.numpy().T.astype("<f4")
        first_bias = model.fc1.bias.numpy().astype("<f4")
        second_weights = model.fc2.weight.numpy().T.astype("<f4")
        second_bias = model.fc2.bias.numpy().astype("<f4")
        output_weights = model.out.weight.numpy().reshape(-1).astype("<f4")
        output_bias = np.asarray([model.out.bias.item()], dtype="<f4")

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as handle:
        handle.write(struct.pack(
            "<8s5If",
            b"THNNUE01",
            1,
            INPUT_FEATURES,
            HIDDEN_SIZE,
            OUTPUT_HIDDEN_SIZE,
            0,
            OUTPUT_SCALE,
        ))
        for values in (
            first_weights,
            first_bias,
            second_weights,
            second_bias,
            output_weights,
            output_bias,
        ):
            values.tofile(handle)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", nargs="+", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=24)
    parser.add_argument("--batch-size", type=int, default=1024)
    parser.add_argument("--learning-rate", type=float, default=1e-3)
    parser.add_argument("--seed", type=int, default=20260814)
    parser.add_argument(
        "--label-perspective",
        choices=("white", "side-to-move"),
        default="white",
        help=(
            "perspective of the probabilities in the input files "
            "(default: white)"
        ),
    )
    args = parser.parse_args()

    if args.epochs < 1 or args.batch_size < 1 or args.learning_rate <= 0:
        raise ValueError("epochs, batch-size, and learning-rate must be positive")

    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.set_num_threads(max(1, min(8, torch.get_num_threads())))

    features, targets = load_rows(args.data, args.label_perspective)
    order = np.random.default_rng(args.seed).permutation(len(features))
    split = max(1, int(len(order) * 0.9))
    train_order = order[:split]
    valid_order = order[split:]

    train_data = TensorDataset(
        torch.from_numpy(features[train_order]),
        torch.from_numpy(targets[train_order]),
    )
    valid_data = TensorDataset(
        torch.from_numpy(features[valid_order]),
        torch.from_numpy(targets[valid_order]),
    )
    loader = DataLoader(train_data, batch_size=args.batch_size, shuffle=True)
    valid_loader = DataLoader(valid_data, batch_size=args.batch_size)

    model = ThetaNNUE()
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=args.learning_rate, weight_decay=1e-5
    )
    loss_fn = nn.SmoothL1Loss(beta=0.05)

    for epoch in range(args.epochs):
        model.train()
        train_total = 0.0
        for batch_features, batch_targets in loader:
            optimizer.zero_grad(set_to_none=True)
            loss = loss_fn(model(batch_features), batch_targets)
            loss.backward()
            optimizer.step()
            train_total += loss.item() * len(batch_features)

        model.eval()
        valid_total = 0.0
        with torch.no_grad():
            for batch_features, batch_targets in valid_loader:
                valid_total += loss_fn(
                    model(batch_features), batch_targets
                ).item() * len(batch_features)
        print(
            f"epoch {epoch + 1}/{args.epochs} "
            f"train={train_total / len(train_data):.6f} "
            f"valid={valid_total / len(valid_data):.6f}"
        )

    export_model(model, args.output)
    print(f"positions={len(features)} output={args.output}")


if __name__ == "__main__":
    main()
