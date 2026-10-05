#!/usr/bin/env python3
"""Throughput bar chart for the AVX-512 lexer vs the flex baseline.

The numbers below are medians of 3 Google Benchmark repetitions
(--benchmark_min_time=0.3s) taken on an 8-core Intel Core i7-11370H with
GCC 16.2, -O3 -march=native, Release, flex 2.6.4 --full.  They are the same
values reported in benchmark/README.md and in Table 1 of research.lex, so the
figure, the table and the harness output cannot drift apart.

Regenerate with:
    ./benchmark/run.sh -- --benchmark_repetitions=3 --benchmark_min_time=0.3s \\
        --benchmark_out=results.json --benchmark_out_format=json
    python3 benchmark/tools/throughput_chart.py

Usage:
    python3 throughput_chart.py [-o throughput_chart.pdf]
"""

from __future__ import annotations

import argparse
import pathlib

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np

CORPORA: list[tuple[str, int, float, float, float]] = [
    ("real-src", 117_828, 0.3295, 0.1996, 1.63),
    ("code-small", 266_394, 0.5380, 0.2535, 2.12),
    ("code-large", 4_212_693, 0.5738, 0.2648, 2.16),
    ("comment-large", 4_195_860, 0.7035, 0.2997, 2.35),
]

SIZE_LABEL = {
    "real-src": "116 KiB",
    "code-small": "260 KiB",
    "code-large": "4.0 MiB",
    "comment-large": "4.0 MiB",
}

OURS_COLOR = "#14508C"
BASE_COLOR = "#E0A458"
EDGE_COLOR = "#1A1A1A"

MONKEY_STYLE = dict(
    color=OURS_COLOR, edgecolor=EDGE_COLOR, hatch="///", linewidth=0.9, zorder=3
)
FLEX_STYLE = dict(
    color=BASE_COLOR, edgecolor=EDGE_COLOR, hatch="\\\\", linewidth=0.9, zorder=3
)

INK = "#1A1A1A"
MUTED_INK = "#5A5A5A"
GRID = "#D9D9D9"
ACCENT_TEXT = OURS_COLOR


def build_figure() -> plt.Figure:
    plt.style.use("seaborn-v0_8-whitegrid")

    labels = [c[0] for c in CORPORA]
    monkey = [c[2] for c in CORPORA]
    flex = [c[3] for c in CORPORA]
    speedups = [c[4] for c in CORPORA]

    x = np.arange(len(labels), dtype=float)
    width = 0.36
    gap = 0.012
    base_x = x - width / 2 - gap
    ours_x = x + width / 2 + gap

    fig, ax = plt.subplots(figsize=(7.2, 4.4))

    ax.bar(
        base_x,
        flex,
        width,
        label="flex baseline (scalar DFA)",
        **FLEX_STYLE,
    )
    ax.bar(
        ours_x,
        monkey,
        width,
        label="AVX-512 lexer (this work)",
        **MONKEY_STYLE,
    )

    pad = max(monkey) * 0.018
    for i, (m, f, sp) in enumerate(zip(monkey, flex, speedups)):
        ax.text(
            ours_x[i],
            m + pad,
            f"{m:.3f}",
            ha="center",
            va="bottom",
            fontsize=8.5,
            fontweight="bold",
            color=ACCENT_TEXT,
            zorder=4,
        )
        ax.text(
            base_x[i],
            f + pad,
            f"{f:.3f}",
            ha="center",
            va="bottom",
            fontsize=8.5,
            color=MUTED_INK,
            zorder=4,
        )
        ax.text(
            x[i],
            max(m, f) + pad * 4.5,
            f"{sp:.2f}$\\times$",
            ha="center",
            va="bottom",
            fontsize=9.5,
            fontweight="bold",
            color=INK,
            zorder=4,
        )

    ax.set_xticks(x)
    ax.set_xticklabels(
        [f"{lab}\n{SIZE_LABEL[lab]}" for lab in labels], fontsize=9
    )
    for lbl in ax.get_xticklabels():
        lbl.set_color(INK)
    ax.set_ylabel("Throughput (GB/s, decimal)", fontsize=10, color=INK)
    ax.set_xlabel("Corpus", fontsize=10, color=INK)
    ax.set_ylim(0, max(monkey) * 1.24)

    ax.yaxis.grid(True, color=GRID, linewidth=0.7)
    ax.xaxis.grid(False)
    ax.set_axisbelow(True)

    ax.tick_params(axis="both", colors=MUTED_INK, labelsize=9)

    legend = ax.legend(
        loc="upper left",
        frameon=True,
        framealpha=1.0,
        edgecolor=GRID,
        facecolor="white",
        fontsize=9,
    )
    legend.get_frame().set_linewidth(0.8)
    for txt in legend.get_texts():
        txt.set_color(INK)

    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
        ax.spines[side].set_linewidth(0.9)

    title = ax.set_title(
        "Lexing throughput: AVX-512 lexer vs flex baseline\n"
        "i7-11370H, GCC 16.2, -O3 -march=native, Release, medians of 3 runs",
        fontsize=10.5,
    )
    title.set_color(INK)
    title.set_fontweight("bold")

    fig.tight_layout()
    return fig


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-o",
        "--output",
        default="throughput_chart.pdf",
        type=pathlib.Path,
        help="output file (default: throughput_chart.pdf)",
    )
    args = parser.parse_args()

    fig = build_figure()
    fig.savefig(args.output, format="pdf", bbox_inches="tight")
    fig.savefig(args.output.with_suffix(".png"), dpi=200, bbox_inches="tight")
    print(f"wrote {args.output} and {args.output.with_suffix('.png')}")


if __name__ == "__main__":
    main()