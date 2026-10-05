# Lexer benchmarks: `src/lexer.cppm` vs flex

`src/lexer.cppm` is a hand written AVX-512 lexer. This directory times it
against a flex implementation of the same language, so the numbers have
something to compare against that isn't just the same code run twice.

Both lexers run over the same bytes and every token is compared: type, text and
line number. If they disagree the harness prints the mismatches and refuses to
measure anything. The token structs are `static_assert`ed to be the same size
and layout as `token::Token`, and both sides use the same `mmap` recipe (one
extra zero filled page past the end of the file) and the same token capacity
heuristic (`size / 3 + 16`), so neither side wins on bookkeeping.

flex reads the mapping directly through `yy_scan_buffer()` rather than
`stdio`, so it isn't charged a `memcpy` that the hand written lexer avoids.

## Results

8-core Intel Core i7-11370H (AVX-512), GCC 16.2, `-O3 -march=native`, Release,
flex 2.6.4 with `--full`. Medians of 3 repetitions at
`--benchmark_min_time=0.3s`. Ratio is flex time / monkey time:

| corpus | size | Monkey/Lex | Flex/Lex | ratio | Monkey/Pipeline | Flex/Pipeline | ratio |
|---|---|---:|---:|---:|---:|---:|---:|
| real-src | 115 kB | 314 MiB/s | 190 MiB/s | 1.63x | 296 MiB/s | 180 MiB/s | 1.65x |
| code-small | 260 kB | 513 MiB/s | 242 MiB/s | 2.12x | 497 MiB/s | 226 MiB/s | 2.20x |
| code-large | 4114 kB | 547 MiB/s | 253 MiB/s | 2.16x | 348 MiB/s | 205 MiB/s | 1.69x |
| comment-large | 4098 kB | 671 MiB/s | 286 MiB/s | 2.35x | 431 MiB/s | 221 MiB/s | 1.95x |

Throughput is `bytes_per_second` from Google Benchmark. Token counts per run are
in `tokens_per_run`. To reproduce:

```sh
./benchmark/run.sh -- --benchmark_repetitions=3 --benchmark_min_time=0.3s
```

`real-src` is where the AVX-512 lexer looks worst. This repository's own sources
are about 15% comment and whitespace, so the vector loops bail out early and the
time goes into per-token bookkeeping instead.

Pipeline narrows the gap because `mmap`/`munmap` costs roughly 3.5 ms for a 4 MiB
file, and both lexers pay it.

## Hardware counters

`perf stat -e instructions,cycles,branch-misses,branches`, pinned with
`taskset -c 2`. Run with `--benchmark_min_time=Nx` so both lexers process the
same byte count, which is what makes the per-byte columns comparable:

| corpus | IPC (flex / ours) | branch-miss % (flex / ours) | instr/byte (flex / ours) | cycles/byte (flex / ours) |
| --- | --- | --- | --- | --- |
| `real-src` | 1.75 / 1.63 | 3.0 / 2.4 | 32.7 / 19.0 | 18.7 / 11.6 |
| `code-small` | 1.51 / 1.30 | 2.6 / 5.4 | 23.0 / 9.0 | 15.2 / 6.9 |
| `code-large` | 1.48 / 1.20 | 2.4 / 6.6 | 21.4 / 7.7 | 14.5 / 6.5 |
| `comment-large` | 1.71 / 1.20 | 2.2 / 6.2 | 24.1 / 7.3 | 14.1 / 6.0 |

IPC goes the wrong way for the AVX-512 lexer on every corpus, and the branch
miss rate is worse on three of four. The wide instructions are limited by vector
port throughput, while the DFA's per-character transitions predict well.

The difference is instructions per byte: 0.30x to 0.58x of flex, geometric mean
0.40x. That is the per-token bookkeeping getting amortized across 64-byte
chunks. Branches per retired instruction drop from ~0.21 to ~0.14, and what's
left is the irregular work at token boundaries.

Cycles per byte implies 2.21x / 1.60x / 2.24x / 2.33x, close to the wall-clock
numbers.

```sh
cd <build> && taskset -c 2 perf stat -e instructions,cycles,branch-misses,branches \
  ./lexer_bench --no-verify --benchmark_filter='Monkey/Lex/code-large' --benchmark_min_time=1000x
```

`--no-verify` is needed because verification runs the reference scanner once,
and those branches would land in the counts.

## Requirements

- CMake 4.2 and **Ninja** (or Visual Studio 17.4+). The lexer is a C++26 module
  and CMake can't build modules with Makefiles.
- A C++26 compiler with AVX-512, since the benchmark builds with `-march=native`.
  Tested with GCC 16.2.
- `flex`: `apt install flex`, `dnf install flex`, or `brew install flex`.
- Google Benchmark, via `find_package(benchmark)` if you have `libbenchmark-dev`
  installed, otherwise fetched with `FetchContent`.

## Running it

```sh
./benchmark/run.sh                          # configure, build, verify, measure
./benchmark/run.sh --verify-only            # just check the two lexers agree
./benchmark/run.sh -- --benchmark_min_time=2s --benchmark_repetitions=5
```

Flags before `--` go to the harness, flags after it to Google Benchmark. By
hand:

```sh
cmake -S benchmark -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/bench
./build/bench/lexer_bench --verify-only
./build/bench/lexer_bench --benchmark_out=results.json --benchmark_out_format=json
./benchmark/tools/compare.py results.json
```

Or as part of the compiler build:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMONKEY_BUILD_BENCHMARKS=ON
cmake --build build
./build/benchmark/lexer_bench
```

Harness flags:

| flag | meaning |
| --- | --- |
| `--verify-only` | compare token streams and exit, also what `ctest` runs |
| `--generate-only` | write the corpora and exit |
| `--no-verify` | skip the comparison and measure anyway, debugging only |
| `--list` | list the corpora and their sizes |
| `--corpus-dir DIR` | where the corpora live, defaults to the build directory |
| `--small-bytes N`, `--large-bytes N` | size of the generated corpora |
| `--quiet` | only print problems |

## What gets measured

| benchmark | includes |
| --- | --- |
| `Monkey/Lex/*`, `Flex/Lex/*` | tokenising only; mapping, token buffer and scanner state are built once |
| `Monkey/Pipeline/*`, `Flex/Pipeline/*` | `open` + `mmap` + `madvise` + tokenise + `munmap`, the per-file cost in a real compile |

## Corpora

`corpus/corpus.hpp` generates them from a fixed seed and only rewrites a file
when the bytes actually change:

| corpus | what it is |
| --- | --- |
| `edge` | hand written pathological input: keyword prefixes, `**/`, `/*/*`, glued constants, unsupported bytes, an unterminated block comment, embedded NUL bytes. Verified, never timed, at 1.3 kB |
| `real-src` | this repository's `src/*.cppm` concatenated |
| `code-small` / `code-large` | generated code, few comments, 256 KiB and 4 MiB |
| `comment-large` | generated, about 45% comments and whitespace, 4 MiB |

The generated corpora stay inside what Monkey can tokenise, so no `[`, no digit
suffixes, no unsupported bytes. Those would all produce `Error` tokens and you'd
be measuring the error path. `edge` covers that instead.

## flex options

| option | default | effect |
| --- | --- | --- |
| `MONKEY_FLEX_FULL` | `ON` | passes `--full`, dropping flex's input equivalence classes |
| `MONKEY_FLEX_KEYWORD_RULES` | `ON` | one rule per keyword, which is the usual way to write it and lets the DFA match keywords for free. `OFF` gives the identifier rule the same AVX-512 keyword map `src/lexer.cppm` uses, so flex isn't carrying a table the hand written lexer doesn't pay for |

```sh
cmake -S benchmark -B build/bench -G Ninja -DMONKEY_FLEX_KEYWORD_RULES=OFF
```

## Layout

```
benchmark/
  include/monkey_token_types.h  token codes, shared by both lexers (X-macro list)
  include/monkey_flex_scanner.h  the reentrant flex entry points we call
  include/monkey_flex_lexer.h    C driver API + layout compatible token struct
  flex/monkey_lex.l              the scanner: same rules, same order, same codes
  flex/flex_lexer.cpp            mmap + yy_scan_buffer driver
  corpus/corpus.hpp              deterministic corpus generation
  bench/bench_lexer.cpp          verification + Google Benchmark harness
  tools/compare.py               pairs the two JSON results into one table
  tools/throughput_chart.py      throughput chart, PDF and PNG
  run.sh                         configure, build, verify, measure
  research.lex                   LaTeX draft, takes its numbers from here
  CMakeLists.txt                 standalone project, also add_subdirectory()able
```

Token codes live in exactly one place, `include/monkey_token_types.h`, and
`bench/bench_lexer.cpp` asserts every entry against the real
`token::TokenType`. Renumber `src/token.cppm` and the benchmark build stops.

The harness compiles `src/token.cppm`, `src/ascii.cppm` and `src/lexer.cppm` as
they are, no copies.

