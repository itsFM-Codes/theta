#!/usr/bin/env python3
"""Convert a PGN self-play collection into a white-perspective Texel dataset.

Each emitted row contains the game result followed by the FEN before a move.
Keeping the label in White's perspective matches Theta's component exporter,
which emits white-perspective evaluation components.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import chess.pgn


def result_value(result: str) -> float | None:
    if result == "1-0":
        return 1.0
    if result == "0-1":
        return 0.0
    if result == "1/2-1/2":
        return 0.5
    return None


def convert(
    source: Path,
    destination: Path,
    stride: int,
    skip_plies: int,
    skip_games: int,
    max_games: int | None,
) -> tuple[int, int]:
    games = 0
    seen_games = 0
    positions = 0
    destination.parent.mkdir(parents=True, exist_ok=True)

    with source.open("r", encoding="utf-8", errors="replace") as pgn, destination.open(
        "w", encoding="utf-8", newline="\n"
    ) as output:
        output.write("# result fen\n")
        while True:
            game = chess.pgn.read_game(pgn)
            if game is None:
                break

            label = result_value(game.headers.get("Result", ""))
            if label is None:
                continue

            if seen_games < skip_games:
                seen_games += 1
                continue
            if max_games is not None and games >= max_games:
                break

            seen_games += 1
            games += 1
            board = game.board()
            ply = 0
            for move in game.mainline_moves():
                if ply >= skip_plies and (ply - skip_plies) % stride == 0:
                    output.write(f"{label:.1f} {board.fen()}\n")
                    positions += 1
                board.push(move)
                ply += 1

    return games, positions


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pgn", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--stride", type=int, default=2, help="Keep one position every N plies")
    parser.add_argument(
        "--skip-plies",
        type=int,
        default=8,
        help="Skip the opening plies of every game before sampling",
    )
    parser.add_argument("--skip-games", type=int, default=0)
    parser.add_argument("--max-games", type=int, default=None)
    args = parser.parse_args()
    if args.stride < 1:
        parser.error("--stride must be at least 1")
    if args.skip_plies < 0:
        parser.error("--skip-plies must be non-negative")
    if args.skip_games < 0:
        parser.error("--skip-games must be non-negative")
    if args.max_games is not None and args.max_games < 1:
        parser.error("--max-games must be positive")

    games, positions = convert(
        args.pgn,
        args.output,
        args.stride,
        args.skip_plies,
        args.skip_games,
        args.max_games,
    )
    print(f"converted {games} games and {positions} positions")


if __name__ == "__main__":
    main()
