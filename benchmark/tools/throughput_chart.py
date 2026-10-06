#!/usr/bin/env python3
"""Two-panel throughput chart: ablation ladder and baseline variants.

The numbers below are medians of 7 Google Benchmark repetitions
(--benchmark_min_time=1s, pinned to one core) taken on an Intel Core i7-11370H
with GCC 16.2, Release.  They are the same values reported in the ablation and
baseline tables of research.lex, so the figure, the tables and the harness
output cannot drift apart.  Fill them from the benchmark session log before
rendering; the script refuses to draw a chart with missing entries.

Regenerate with:
    taskset -c 2 ./lexer_bench --quiet \\
        --benchmark_filter='^(Scalar|Masks|Idents|Flat|FlatIdents|Monkey|Flex|FlexAVXKW|FlexCF|Re2c)/Lex/' \\
        --benchmark_min_time=1s --benchmark_repetitions=7 \\
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

CORPORA: tuple[str, ...] = (
    "real-src",
    "real-libstdcxx",
    "code-small",
    "code-large",
    "comment-large",
)

SIZE_LABEL = {
    "real-src": "115 KiB",
    "real-libstdcxx": "4.7 MiB (ext.)",
    "code-small": "260 KiB",
    "code-large": "4.0 MiB",
    "comment-large": "4.0 MiB",
}

# Decimal GB/s per corpus, in CORPORA order.  Must match research.lex.
ABLATION: dict[str, tuple[float | None, ...]] = {
    "Scalar": (0.3707, 0.5406, 0.5699, 0.6709, 0.7114),
    "Masks": (0.2816, 0.4608, 0.5332, 0.6226, 0.6735),
    "Idents": (0.4531, 0.6333, 0.6226, 0.6897, 0.7735),
    "Flat": (0.3871, 0.5695, 0.6499, 0.7321, 0.8153),
    "FlatIdents": (0.4701, 0.6015, 0.6385, 0.7347, 0.7828),
    "Monkey": (0.3682, 0.5455, 0.6210, 0.6281, 0.7462),
    "Flex": (0.2184, 0.2699, 0.2789, 0.2904, 0.3024),
}

BASELINE: dict[str, tuple[float | None, ...]] = {
    "Monkey": (0.3682, 0.5455, 0.6210, 0.6281, 0.7462),
    "Flex": (0.2184, 0.2699, 0.2789, 0.2904, 0.3024),
    "FlexAVXKW": (0.2083, 0.2337, 0.2611, 0.2938, 0.3040),
    "FlexCF": (0.2117, 0.2314, 0.2889, 0.3052, 0.3107),
    "Re2c": (0.1483, 0.1949, 0.2906, 0.3306, 0.3856),
}

OURS_COLOR = "#14508C"
BASE_COLOR = "#E0A458"
EDGE_COLOR = "#1A1A1A"

ABLATION_STYLE = {
    "Scalar": dict(color="#C9DBEF", hatch=""),
    "Masks": dict(color="#9BBCE0", hatch=""),
    "Idents": dict(color="#5F95CB", hatch=""),
    "Flat": dict(color="#2E6FA8", hatch="-"),
    "FlatIdents": dict(color="#7FAAD8", hatch="x"),
    "Monkey": dict(color=OURS_COLOR, hatch="/"),
    "Flex": dict(color=BASE_COLOR, hatch="\\"),
}

BASELINE_STYLE = {
    "Monkey": dict(color=OURS_COLOR, hatch="/"),
    "Flex": dict(color=BASE_COLOR, hatch="\\"),
    "FlexAVXKW": dict(color="#F1CB8E", hatch="\\"),
    "FlexCF": dict(color="#C48840", hatch="\\"),
    "Re2c": dict(color="#8C8C8C", hatch="x"),
}

INK = "#1A1A1A"
MUTED_INK = "#5A5A5A"
GRID = "#D9D9D9"


def validate() -> None:
    for group in (ABLATION, BASELINE):
        for name, values in group.items():
            if len(values) != len(CORPORA) or any(v is None for v in values):
                raise SystemExit(
                    f"missing throughput data for {name}; fill it in "
                    "benchmark/tools/throughput_chart.py from the benchmark "
                    "session before rendering"
                )


def add_group(ax: plt.Axes, names: list[str], data: dict[str, tuple], x: np.ndarray) -> None:
    count = len(names)
    width = 0.86 / count
    for i, name in enumerate(names):
        offset = (i - (count - 1) / 2) * width
        style = (ABLATION_STYLE if data is ABLATION else BASELINE_STYLE)[name]
        ax.bar(
            x + offset,
            data[name],
            width * 0.94,
            label=name,
            color=style["color"],
            edgecolor=EDGE_COLOR,
            hatch=style["hatch"],
            linewidth=0.9,
            zorder=3,
        )


def build_figure() -> plt.Figure:
    plt.style.use("seaborn-v0_8-whitegrid")

    x = np.arange(len(CORPORA), dtype=float)
    vmax = max(v for group in (ABLATION, BASELINE) for values in group.values() for v in values)

    fig, (ax_a, ax_b) = plt.subplots(1, 2, figsize=(9.8, 4.3), sharey=True)

    add_group(ax_a, list(ABLATION), ABLATION, x)
    add_group(ax_b, list(BASELINE), BASELINE, x)

    for ax, panel in ((ax_a, "Ablation ladder"), (ax_b, "Baseline variants")):
        ax.set_xticks(x)
        ax.set_xticklabels(
            [f"{lab}\n{SIZE_LABEL[lab]}" for lab in CORPORA], fontsize=8.5
        )
        for lbl in ax.get_xticklabels():
            lbl.set_color(INK)
        ax.set_ylim(0, vmax * 1.32)
        ax.yaxis.grid(True, color=GRID, linewidth=0.7)
        ax.xaxis.grid(False)
        ax.set_axisbelow(True)
        ax.tick_params(axis="both", colors=MUTED_INK, labelsize=8.5)
        title = ax.set_title(panel, loc="left", fontsize=9.5)
        title.set_color(INK)
        title.set_fontweight("bold")
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
        for side in ("left", "bottom"):
            ax.spines[side].set_color(GRID)
            ax.spines[side].set_linewidth(0.9)
        legend = ax.legend(
            loc="upper center",
            ncol=4 if ax is ax_a else len(BASELINE),
            frameon=True,
            framealpha=0.95,
            edgecolor=GRID,
            facecolor="white",
            fontsize=8,
            columnspacing=1.0,
            handletextpad=0.5,
        )
        legend.get_frame().set_linewidth(0.8)
        for txt in legend.get_texts():
            txt.set_color(INK)

    ax_a.set_ylabel("Throughput (GB/s, decimal)", fontsize=10, color=INK)
    ax_b.set_xlabel("Corpus", fontsize=10, color=INK)

    fig.suptitle(
        "Lexing throughput: ablation ladder and baseline variants\n"
        "i7-11370H, GCC 16.2, medians of 7 runs at 1s, pinned to one core",
        fontsize=10.5,
        fontweight="bold",
        color=INK,
        y=0.985,
    )
    fig.tight_layout(rect=(0, 0, 1, 0.935))
    return fig


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-o",
        "--output",
        default=pathlib.Path(__file__).resolve().parent.parent / "throughput_chart.pdf",
        type=pathlib.Path,
        help="output file (default: benchmark/throughput_chart.pdf, resolved "
        "relative to this script rather than the current directory)",
    )
    args = parser.parse_args()

    validate()
    fig = build_figure()
    fig.savefig(args.output, format="pdf", bbox_inches="tight")
    fig.savefig(args.output.with_suffix(".png"), dpi=200, bbox_inches="tight")
    print(f"wrote {args.output} and {args.output.with_suffix('.png')}")


if __name__ == "__main__":
    main()
