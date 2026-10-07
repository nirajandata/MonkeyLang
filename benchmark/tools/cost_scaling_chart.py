#!/usr/bin/env python3
"""Plot counter-derived speedup against bytes per emitted token."""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt

CORPORA = ("real-src", "real-libstdcxx", "code-small", "code-large", "comment-large")
BYTES_PER_TOKEN = (4.1, 8.0, 8.6, 10.1, 12.1)
COUNTER_SPEEDUP = (1.68, 1.87, 2.15, 2.35, 2.44)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "cost_scaling_chart",
        help="output path without an extension",
    )
    args = parser.parse_args()

    plt.style.use("seaborn-v0_8-whitegrid")
    fig, ax = plt.subplots(figsize=(6.8, 3.6), constrained_layout=True)
    ax.plot(
        BYTES_PER_TOKEN,
        COUNTER_SPEEDUP,
        color="#14508C",
        marker="o",
        linewidth=1.8,
        markersize=6,
    )
    for x, y, label in zip(BYTES_PER_TOKEN, COUNTER_SPEEDUP, CORPORA):
        ax.annotate(
            label,
            (x, y),
            xytext=(5, 7),
            textcoords="offset points",
            fontsize=8,
        )
    ax.set_xlabel("Input bytes per emitted token")
    ax.set_ylabel("Flex / Monkey cycles per byte")
    ax.set_ylim(1.4, 2.7)
    ax.set_xlim(3.4, 13)
    ax.grid(axis="y", color="#D9D9D9", linewidth=0.7)
    ax.grid(axis="x", visible=False)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output.with_suffix(".png"), dpi=200)
    fig.savefig(args.output.with_suffix(".pdf"))


if __name__ == "__main__":
    main()
