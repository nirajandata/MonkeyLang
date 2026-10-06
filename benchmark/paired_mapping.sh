#!/usr/bin/env bash
#   ./benchmark/paired_mapping.sh [pairs] [cpu]
#
# Paired Monkey/Copy sampling for Section "Isolating the padded mapping".
#
# Whole session medians are unstable for the Pipeline shape, so this alternates
# the two variants within each pair and reports the median of the per-pair
# throughput ratios, which cancels drift and page-cache state instead of
# averaging over it. Writes one JSON per launch to $out_dir.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$root/build/bench}"
bench="$build_dir/lexer_bench"

pairs="${1:-12}"
cpu="${2:-2}"
shape="${SHAPE:-Pipeline}"
variants="${VARIANTS:-Monkey Copy}"
min_time="${MIN_TIME:-0.3s}"
out_dir="${OUT_DIR:-${TMPDIR:-/tmp}/monkey-paired-$shape}"

if [[ ! -x "$bench" ]]; then
    echo "no benchmark binary at $bench" >&2
    echo "  build it first: cmake -S benchmark -B build/bench -G Ninja && cmake --build build/bench" >&2
    exit 1
fi

mkdir -p "$out_dir"

for corpus in real-src real-libstdcxx code-small code-large comment-large; do
    for ((i = 1; i <= pairs; i++)); do
        for variant in $variants; do
            taskset -c "$cpu" "$bench" \
                --benchmark_min_time="$min_time" \
                --benchmark_repetitions=1 \
                --benchmark_filter="$variant/$shape/$corpus" \
                --benchmark_out_format=json \
                --benchmark_out="$out_dir/${variant}_${corpus}_${i}.json" \
                > /dev/null 2>&1
        done
    done
    echo "  $corpus: $pairs pairs"
done

nlaunch=$((pairs * 5 * $(wc -w <<< "$variants")))
echo "wrote $nlaunch files to $out_dir"
echo "summarise them with the same paired-median rule the paper quotes:"
echo "  use run_type=iteration entries, one JSON per file, ratio Copy/Monkey per pair"