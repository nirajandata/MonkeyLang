#!/usr/bin/env python3
"""Generate fixed-byte identifier corpora with independent token/run lengths."""

from __future__ import annotations

import argparse
import random
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--bytes", type=int, default=4 * 1024 * 1024)
    parser.add_argument("--seed", type=int, default=20261007)
    args = parser.parse_args()
    if args.bytes <= 0:
        parser.error("--bytes must be positive")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for token_length in (4, 16, 64):
        for whitespace_length in (1, 8, 32):
            name = f"grid-random-id{token_length}-ws{whitespace_length}"
            rng = random.Random(
                args.seed + token_length * 1_000 + whitespace_length
            )
            runs = list(range(1, 2 * whitespace_length))
            content = bytearray()
            run_index = 0
            while len(content) < args.bytes:
                identifier = "".join(
                    rng.choice("abcdefghijklmnopqrstuvwxyz")
                    for _ in range(token_length)
                )
                if run_index == len(runs):
                    rng.shuffle(runs)
                    run_index = 0
                content.extend(identifier.encode("ascii"))
                content.extend(b" " * runs[run_index])
                run_index += 1
            del content[args.bytes :]
            path = args.output_dir / f"{name}.txt"
            path.write_bytes(content)
            print(
                f"{name}={path} ({len(content)} bytes, seed "
                f"{args.seed + token_length * 1_000 + whitespace_length})"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
