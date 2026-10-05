#!/usr/bin/env python3
"""Pairs the Monkey and Flex Google Benchmark results into one table.

    ./lexer_bench --benchmark_out_format=json --benchmark_out=results.json
    tools/compare.py results.json

Each corpus yields four benchmarks (Lex = mapping reused, Pipeline = open +
mmap + lex + unmap), reduced to two ratios plus both throughputs. A run without
a Flex counterpart, e.g. --benchmark_filter=Monkey/..., is reported as missing
rather than silently dropped.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field

VARIANTS = ("Lex", "Pipeline")

REPEAT_SUFFIXES = {"mean": 2, "median": 2, "stddev": 0, "cv": 0}


@dataclass
class Result:
    real_time: float
    bytes_per_second: float | None = None
    tokens_per_run: float | None = None
    priority: int = 1


@dataclass
class Corpus:
    name: str = ""
    corpus_bytes: int = 0
    monkey: dict[str, Result] = field(default_factory=dict)
    flex: dict[str, Result] = field(default_factory=dict)

    def ratio(self, variant: str) -> float | None:
        """Flex time / Monkey time: > 1 means Monkey is faster."""
        ours = self.monkey.get(variant)
        theirs = self.flex.get(variant)
        if ours is None or theirs is None or ours.real_time <= 0:
            return None
        return theirs.real_time / ours.real_time


def add(table: dict[str, Result], variant: str, result: Result) -> None:
    previous = table.get(variant)
    if previous is None or result.priority >= previous.priority:
        table[variant] = result


def load(path: str) -> list[Corpus]:
    with open(path, encoding="utf-8") as handle:
        raw = json.load(handle)

    corpora: dict[str, Corpus] = {}
    ignored: list[str] = []

    for entry in raw.get("benchmarks", []):
        name = entry.get("name", "")
        parts = name.split("/")
        if len(parts) != 3:
            continue

        lexer, variant, corpus_name = parts
        priority = 1
        suffix = corpus_name.rpartition("_")[2]
        if suffix in REPEAT_SUFFIXES:
            priority = REPEAT_SUFFIXES[suffix]
            corpus_name = corpus_name.rpartition("_")[0]
        if priority == 0:
            continue

        if lexer not in ("Monkey", "Flex") or variant not in VARIANTS:
            ignored.append(name)
            continue

        result = Result(
            real_time=entry["real_time"],
            bytes_per_second=entry.get("bytes_per_second"),
            tokens_per_run=entry.get("tokens_per_run") or None,
            priority=priority,
        )

        corpus = corpora.setdefault(corpus_name, Corpus(name=corpus_name))
        corpus.corpus_bytes = int(entry.get("corpus_bytes", corpus.corpus_bytes))
        table = corpus.monkey if lexer == "Monkey" else corpus.flex
        add(table, variant, result)

    for name in sorted(set(ignored)):
        print(f"note: ignoring unrecognised benchmark {name!r}", file=sys.stderr)

    return [corpora[name] for name in sorted(corpora)]


def gib(value: float) -> str:
    return f"{value / (1 << 20):.1f} MiB/s"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", help="JSON produced by --benchmark_out")
    parser.add_argument(
        "--min-time",
        type=float,
        default=0.2,
        help="highlight ratios below this factor as noise (default: %(default)s)",
    )
    args = parser.parse_args()

    corpora = load(args.results)
    if not corpora:
        print("no Monkey/Flex benchmarks found in the results file", file=sys.stderr)
        return 1

    width = max(len(c.name) for c in corpora)
    header = (
        f"{'corpus':<{width}}  {'size':>9}"
        f"  {'Monkey/Lex':>12}  {'Flex/Lex':>12}  {'ratio':>6}"
        f"  {'Monkey/Pipe':>12}  {'Flex/Pipe':>12}  {'ratio':>6}"
    )
    print(header)
    print("-" * len(header))

    for corpus in corpora:
        cells = []
        for variant in VARIANTS:
            ours = corpus.monkey.get(variant)
            theirs = corpus.flex.get(variant)
            ratio = corpus.ratio(variant)

            ours_cell = (
                gib(ours.bytes_per_second)
                if ours and ours.bytes_per_second
                else f"{ours.real_time / 1e6:.2f} ms" if ours else "missing"
            )
            theirs_cell = (
                gib(theirs.bytes_per_second)
                if theirs and theirs.bytes_per_second
                else f"{theirs.real_time / 1e6:.2f} ms"
                if theirs
                else "missing"
            )

            if ratio is None:
                ratio_cell = "-"
            elif ratio < 1.0:
                ratio_cell = f"{1.0 / ratio:.2f}x"
            else:
                ratio_cell = f"{ratio:.2f}x"
                if ratio < args.min_time:
                    ratio_cell += "*"

            cells += [ours_cell, theirs_cell, ratio_cell]

        print(
            f"{corpus.name:<{width}}  {corpus.corpus_bytes / 1024:>8.0f}k"
            f"  {cells[0]:>12}  {cells[1]:>12}  {cells[2]:>6}"
            f"  {cells[3]:>12}  {cells[4]:>12}  {cells[5]:>6}"
        )

    print()
    print("ratio = flex time / monkey time, so anything above 1.00x is the "
          "hand written lexer being faster.")
    print("*     = below --min-time, i.e. within the noise of this run.")
    return 0


if __name__ == "__main__":
    sys.exit(main())