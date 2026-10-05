#!/usr/bin/env bash
#   ./benchmark/run.sh                  # configure, build, verify, benchmark
#   ./benchmark/run.sh --verify-only    # only check that both lexers agree
#   BUILD_DIR=build/bench ./benchmark/run.sh -- --benchmark_min_time=1s
#
# Flags after a bare `--` go to Google Benchmark, the rest to the harness.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$root/build/bench}"
generator="${CMAKE_GENERATOR:-Ninja}"
results="$build_dir/results.json"

if ! command -v flex > /dev/null; then
    echo "flex is required to generate the reference scanner" >&2
    echo "  Debian/Ubuntu: sudo apt install flex" >&2
    echo "  Fedora:        sudo dnf install flex" >&2
    echo "  macOS:         brew install flex" >&2
    exit 1
fi

extra=()
if [[ $# -gt 0 && $1 != -- ]]; then
    extra+=("$@")
    shift
fi
if [[ $# -gt 0 && $1 == -- ]]; then
    shift
    benchmark_flags=("$@")
else
    benchmark_flags=()
fi

cmake_args=(-S "$root/benchmark" -B "$build_dir" -G "$generator"
            -DCMAKE_BUILD_TYPE=Release)
if [[ -n "${BENCH_CMAKE_ARGS:-}" ]]; then
    # shellcheck disable=SC2206
    cmake_args+=(${BENCH_CMAKE_ARGS})
fi

echo "==> configuring ($generator, $build_dir)"
cmake "${cmake_args[@]}"

echo "==> building"
cmake --build "$build_dir" -j "$(nproc 2> /dev/null || sysctl -n hw.ncpu)"

echo "==> verifying that both lexers produce the same token stream"
"$build_dir/lexer_bench" --verify-only "${extra[@]+"${extra[@]}"}"

if [[ " ${extra[*]-} " == *" --verify-only "* || " ${extra[*]-} " == *" --generate-only "* || " ${extra[*]-} " == *" --list "* ]]; then
    exit 0
fi

echo "==> benchmarking"
"$build_dir/lexer_bench" \
    --benchmark_out="$results" \
    --benchmark_out_format=json \
    ${benchmark_flags[@]+"${benchmark_flags[@]}"} \
    "${extra[@]+"${extra[@]}"}"

echo
"$root/benchmark/tools/compare.py" "$results"
echo
echo "raw results: $results"