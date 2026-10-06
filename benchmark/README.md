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

Intel Core i7-11370H (AVX-512, 4 cores / 8 threads), GCC 16.2,
`-O3 -march=native`, Release, flex 2.6.4 with `--full`. Medians of 7
repetitions at `--benchmark_min_time=1s`, pinned to core 2. Ratio is flex time /
monkey time. These are the numbers in Table 1 of `research.lex` and in
`tools/throughput_chart.py`:

| corpus | size | Monkey/Lex | Flex/Lex | ratio | Monkey/Pipeline | Flex/Pipeline | ratio |
|---|---|---:|---:|---:|---:|---:|---:|
| real-src | 115 kB | 351.1 MiB/s | 208.3 MiB/s | 1.69x | 351.2 MiB/s | 207.5 MiB/s | 1.69x |
| real-libstdcxx | 4801 kB | 520.2 MiB/s | 257.4 MiB/s | 2.03x | 346.9 MiB/s | 208.6 MiB/s | 1.66x |
| code-small | 260 kB | 592.2 MiB/s | 266.0 MiB/s | 2.23x | 602.1 MiB/s | 259.5 MiB/s | 2.32x |
| code-large | 4114 kB | 599.0 MiB/s | 276.9 MiB/s | 2.16x | 386.7 MiB/s | 206.5 MiB/s | 1.87x |
| comment-large | 4098 kB | 711.6 MiB/s | 288.4 MiB/s | 2.47x | 435.4 MiB/s | 204.6 MiB/s | 2.13x |

Ratios are computed from unrounded medians, so they can differ from the ratio
of the printed columns in the last digit. Lex speedup is a geometric mean of
2.10x, Pipeline 1.92x. Throughput is `bytes_per_second` from Google Benchmark. Token counts per run are in
`tokens_per_run`.

The same session times more than the two lexers in this table: the
`Scalar`/`Masks`/`IdentScan`/`KwOnly`/`Idents`/`Flat`/`FlatIdents` ablations, two
further `flex` configurations (one calling our AVX-512 keyword table, one with
`-CF`), and a `re2c` scanner. They are in the ablation and baseline-variant
tables of `research.lex` and in `tools/throughput_chart.py`. To reproduce:

```sh
cmake -S benchmark -B build/bench -DCMAKE_BUILD_TYPE=Release && cmake --build build/bench
taskset -c 2 ./build/bench/lexer_bench --quiet \
  --benchmark_repetitions=7 --benchmark_min_time=1s
```

`real-src` is where the AVX-512 lexer looks worst. This repository's own sources
are 28% whitespace and 0% comments, the least skippable material of the five
inputs (see Corpora below), so the vector loops bail out early and the
time goes into per-token bookkeeping instead.

Pipeline redistributes the gap because `mmap`/`munmap` costs a fixed 3.6-4.5 ms
for a multi-megabyte file, and a fixed cost penalizes the faster lexer
proportionally more: `code-large` falls from 2.16x to 1.87x while the geometric
mean gives back part of the win, 2.10x on Lex against 1.92x on Pipeline.

## What the speedup is actually made of

The obvious explanation for a 2x win is the vector loops, and the instruction
counts look like they back that up: on `code-large` the shipped lexer retires
7.83 instructions per byte where the scalar ablation retires 14.94. It does not
survive contact with the clock. Medians of 7 repetitions at
`--benchmark_min_time=1s`, MiB/s:

| corpus | Scalar | Masks | KwOnly | IdentScan | Idents | Flat | FlatIdents | Monkey | Flex |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `real-src` | 353.5 | 268.6 | 367.8 | 405.4 | 432.1 | 369.2 | 448.3 | 351.1 | 208.3 |
| `real-libstdcxx` | 515.6 | 439.5 | 520.6 | 577.8 | 604.0 | 543.1 | 573.6 | 520.2 | 257.4 |
| `code-small` | 543.5 | 508.5 | 482.4 | 561.6 | 593.8 | 619.8 | 608.9 | 592.2 | 266.0 |
| `code-large` | 639.8 | 593.8 | 603.2 | 652.8 | 657.7 | 698.2 | 700.7 | 599.0 | 276.9 |
| `comment-large` | 678.4 | 642.3 | 643.8 | 640.4 | 737.7 | 777.5 | 746.5 | 711.6 | 288.4 |

`Scalar` is the same lexer with every 64-byte loop replaced by a byte-at-a-time
loop, and it runs level with the shipped lexer, 0.99x of it on geometric mean and
within 1% on each real-world corpus. The split cells take the small difference
that remains apart: `IdentScan` vectorizes the identifier/constant scan but keeps
keyword matching scalar (1.05x of `Scalar`), `KwOnly` keeps the scalar scan and
adds the packed keyword table (0.96x of `Scalar`), `Idents` has both (1.10x of
the shipped lexer), and `FlatIdents` adds the per-window cached masks on top
(1.12x of the shipped lexer, the highest geometric mean of the ladder and
ahead of the shipped lexer on every corpus). The shipped vectorization does not buy throughput
on this processor, and the best use of the vector units here is identifiers and
keyword tables over scalar whitespace.

`research.lex`'s "Uncertainty and cross-method agreement" section puts 95%
bootstrap intervals on every session cell (±0.5-9.9% per corpus) and
cross-checks them against paired runs: `Idents` beats the shipped lexer in
60/60 paired runs, `FlatIdents` in 52/60, `Scalar` in 22/60, `Copy` in 20/60,
with session, paired and hardware-counter estimates agreeing to within about
5 percentage points.

The counters say why. Instructions per byte fall by 48% from Scalar to Monkey on
`code-large` (14.94 to 7.83), but IPC falls further, from 2.18 to 1.23, because
the wide loops' dispatched uops concentrate on the vector ports — port 5 (vector
shift/blend/mask-ALU) is busy in 43% of cycles, ports 0 and 1 in 24% and 26% —
while the scalar cell's concentrate on the integer and branch port 6, busy in
56-59% of its cycles. No port exceeds 60% for any lexer, so this is pressure
rather than saturation, but it is measurable: same fixed-iteration `perf`
runs as `research.lex` Table 6:

| corpus | lexer | instr/B | cycles/B | IPC |
| --- | --- | ---: | ---: | ---: |
| `code-large` | Scalar | 14.94 | 6.85 | 2.18 |
| `code-large` | Masks | 9.27 | 7.11 | 1.30 |
| `code-large` | KwOnly | 14.91 | 6.80 | 2.19 |
| `code-large` | IdentScan | 13.53 | 6.57 | 2.06 |
| `code-large` | Idents | 13.55 | 5.99 | 2.26 |
| `code-large` | Flat | 9.18 | 5.86 | 1.57 |
| `code-large` | FlatIdents | 13.48 | 6.35 | 2.12 |
| `code-large` | Monkey | 7.83 | 6.36 | 1.23 |
| `code-large` | Flex | 22.86 | 14.94 | 1.53 |
| `real-libstdcxx` | Scalar | 15.24 | 8.11 | 1.88 |
| `real-libstdcxx` | Monkey | 12.37 | 7.95 | 1.56 |
| `real-libstdcxx` | Flex | 32.88 | 14.84 | 2.22 |

Every counter run handles 3.0 GB of input, so the iteration count is scaled per
corpus (25,461 iterations for the 115 kB corpus). A short fixed-iteration run is
dominated by start-up and inflates the per-byte numbers; that is visible in the
`real-src` row if you use a single fixed count for all corpora.

The win over flex comes from the scan loop's shape, not from using wide
registers: flex retires 22.86 instructions per byte on `code-large` where the
shipped lexer retires 7.83, and both lexers get the same mapping and the same
tokens.

`Masks` is the awkward one. It vectorizes only whitespace and comment skipping
and is *slower* than `Scalar` on every corpus (268.6 against 353.5 on `real-src`),
because a 64-byte load on a four-byte run of whitespace is work thrown away. It
only pays off on input long enough to fill a register.

The ablated lexers are copies in `ablate/`, built as separate modules next to the
real one. They are verified to produce exactly the shipped lexer's token stream,
type, text and line for line, so the comparison is not measuring a lexer that
misbehaves.

## Isolating the padded mapping

Every variant above maps the file, so the ladder says nothing about whether the
mapping is what makes the fast times possible. `Copy` answers that: it is the
shipped lexer with the scan loops byte-for-byte unchanged, reading the file into
a padded `aligned_alloc` buffer with `read` instead of using `MAP_FIXED` and
`MAP_POPULATE`. The buffer is zero-filled only in the padding, so the only
difference from `Monkey` is how the bytes arrive.

This comparison needs a different protocol from the rest of this file. Whole
session medians are not stable for the `Pipeline` shape: two sessions of the same
binary put `Copy` at 0.94x and 1.01x of `Monkey`, with individual corpora moving
by 20%, because each repetition re-derives its iteration count and re-allocates
the mapping while page-cache and frequency state drift. So the numbers below are
**paired**: for each corpus, twelve launches of each variant strictly
interleaved, each a separate process at `--benchmark_min_time=0.3s`, and the
figure is the median of the twelve per-pair ratios.

| corpus | Monkey Lex | Copy Lex | Ratio | Monkey Pipeline | Copy Pipeline | Ratio | Copy wins (Lex) | Copy wins (Pipe) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `real-src` | 366.4 | 360.6 | 0.99x | 344.7 | 345.4 | 1.00x | 2/12 | 8/12 |
| `real-libstdcxx` | 526.6 | 520.2 | 0.98x | 326.7 | 305.2 | 0.94x | 2/12 | 2/12 |
| `code-small` | 564.7 | 570.3 | 1.00x | 577.9 | 578.4 | 1.00x | 8/12 | 6/12 |
| `code-large` | 646.2 | 644.7 | 1.00x | 420.7 | 402.9 | 0.96x | 6/12 | 1/12 |
| `comment-large` | 699.0 | 693.0 | 0.99x | 477.5 | 456.1 | 0.95x | 2/12 | 0/12 |

MiB/s. Lex MiB/s columns are medians of the twelve launches; the Ratio columns
are medians of the per-pair ratios. Pooled over all 60 pairs the median ratio
is 0.99x on Lex, where `Copy` wins 20 of 60 pairs, and 0.97x on `Pipeline`, where
it wins 17 of 60 (2 of 36 on the three multi-megabyte inputs). The counters are
the cleaner evidence for `Lex`: `Copy` and `Monkey` retire identical instructions
per byte, 12.37 on `real-libstdcxx` and 7.83 on `code-large`, with cycles per
byte within 3% (8.28 against 8.08, 6.43 against 6.49). Page faults agree too:
every run took zero major faults, and user-mode minor faults per pipeline
iteration match between the two within 1.3% on all three multi-megabyte corpora
(3,592 against 3,583 on `code-large`, 5,429 against 5,497 on
`real-libstdcxx`, 2,948 against 2,928 on `comment-large`), so fault behaviour
does not separate them either. Those faults are token-buffer first-touch — the
buffer allocates one element per three input bytes, crossing the allocator's
32 MiB mmap threshold only on multi-megabyte inputs, and the small corpora
amortize the run's ~14,000 setup faults over tens of thousands of iterations —
not the mapping, which is populated at mmap time in kernel context.

So the mapping is worth nothing over copying once the buffer is resident, and is
worth about 3% when mapping is charged per file, where `read` copies the page
cache and a populated mapping shares it. On a warm page cache both read from the
same place, so this does not speak to a cold-cache workload. The padding itself is
still doing necessary work: every variant depends on it to skip the load-safety
check.

Two things not to do here: parse `_median` `real_time` from the JSON when
`--benchmark_repetitions` is set. Google Benchmark writes one entry per repetition
under the plain name, plus aggregates named `<name>_median`; reading the plain
name silently gives you whichever repetition happened to be last, which is how an
earlier revision of this file ended up quoting single runs as medians. And do not
carry Lex-shape session medians into this section: the paired protocol exists
precisely because whole-session Pipeline medians drift by more than the effect
being measured.

## Frequency

The concern about 512-bit downclocking does not apply to this part. Sampling
`/sys/devices/system/cpu/cpu2/cpufreq/scaling_cur_freq` every 20 ms for the
lifetime of a pinned `--benchmark_min_time=8s` run, on core 2:

| corpus | Monkey p10 | Monkey median | Flex p10 | Flex median |
| --- | ---: | ---: | ---: | ---: |
| `real-src` | 4.14 | 4.30 | 4.30 | 4.30 |
| `code-small` | 4.22 | 4.30 | 4.30 | 4.30 |
| `code-large` | 4.25 | 4.30 | 4.30 | 4.30 |
| `comment-large` | 4.20 | 4.30 | 3.70 | 4.30 |

`real-libstdcxx` is not in this table because we did not sample frequency during
that run, so there is no measurement to quote for it.

Megahertz. Every median is 4.30 GHz, so no sustained throttle happens under
512-bit load. Monkey's 10th percentile sits 1.2-3.8% below that depending on the
corpus; the p10 dips are not cleanly attributable to AVX-512, because the machine
ran an IDE and a browser on the other cores throughout and `Flex` dips harder
than `Monkey` on `comment-large` (3.70 against 4.20), which is package turbo
sharing rather than anything the shipped lexer does. `research.lex` quotes the
`code-large` rows only. If you want the clock without a sampling thread,
`perf stat -e cycles,instructions` over a `--benchmark_min_time=Nx` run gives the
same story, since the counters here show low IPC rather than a clock drop.

## Pipeline versus Lex

`Pipeline` adds `open`, `mmap`, `madvise` and `munmap`, which is what a compiler
pays per translation unit. Milliseconds per file; Lex columns are from the
seven-repetition session above, Pipeline columns are medians of five:

| corpus | Monkey Lex | Monkey Pipeline | Flex Lex | Flex Pipeline | Lex ratio | Pipeline ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `real-src` | 0.321 | 0.321 | 0.541 | 0.543 | 1.69x | 1.69x |
| `real-libstdcxx` | 9.039 | 13.557 | 18.306 | 22.548 | 2.03x | 1.66x |
| `code-small` | 0.430 | 0.423 | 0.958 | 0.982 | 2.23x | 2.32x |
| `code-large` | 6.738 | 10.448 | 14.548 | 19.538 | 2.16x | 1.87x |
| `comment-large` | 5.637 | 9.251 | 13.910 | 19.666 | 2.47x | 2.13x |

On the multi-megabyte corpora the mapping and teardown gap between the two
columns is 3.6 to 4.5 ms per file for the shipped lexer and 4.2 to 5.8 ms for
the baseline, or 19 to 39% of whichever lexer's pipeline time you measure it
against. A fixed cost penalises the faster lexer proportionally more, which is
why `code-large` falls from 2.16x on `Lex` to 1.87x on `Pipeline`: there the
shipped lexer adds 3.7 ms to a 6.7 ms lex (+55%) while the baseline adds 5.0 ms
to a 14.5 ms lex (+34%). The geometric mean gives part of the win back with it,
2.10x on `Lex` against 1.92x on `Pipeline`, while `real-src` and `code-small`,
whose fixed costs are below the resolution of these runs (-0.007 to +0.024 ms),
move only within session noise. So the pure-lex path represents most of a front end's
per-file cost whenever translation units are small, which is the common case in
C++, and about three fifths to two thirds of it when a single unit is several
megabytes.

Run-to-run coefficients of variation in this session were 0.7-4.3% for the
shipped lexer's pipeline medians and up to 6.5% for flex, so these ratios are
readable without the pairing protocol the mapping comparison needs. The
comparison does not rest on a single session anyway: a paired rerun of flex
against Monkey (twelve interleaved launches per corpus at
`--benchmark_min_time=0.3s`) gives 1.68x / 1.65x / 2.30x / 1.97x / 2.09x per
corpus, a 1.92x pooled geometric mean, and flex wins none of the 60 pairs.
Four of the five rows agree with the table above to within 2%; `code-large`
differs by 5% (1.87x against 1.97x), within session drift.

## Hardware counters

`perf stat -e instructions,cycles,branch-misses,branches`, pinned with
`taskset -c 2`. Run with `--benchmark_min_time=Nx` so both lexers process the
same byte count, which is what makes the per-byte columns comparable:

| corpus | IPC (flex / ours) | branch-miss % (flex / ours) | instr/byte (flex / ours) | cycles/byte (flex / ours) | misses/byte (flex / ours) |
| --- | --- | --- | --- | --- | --- |
| `real-src` | 1.85 / 1.65 | 2.9 / 2.4 | 35.99 / 19.10 | 19.43 / 11.57 | 0.200 / 0.061 |
| `real-libstdcxx` | 2.22 / 1.56 | 2.0 / 3.3 | 32.88 / 12.37 | 14.84 / 7.95 | 0.123 / 0.067 |
| `code-small` | 1.63 / 1.50 | 2.4 / 3.6 | 27.22 / 11.62 | 16.66 / 7.76 | 0.137 / 0.070 |
| `code-large` | 1.53 / 1.23 | 2.5 / 6.3 | 22.86 / 7.83 | 14.94 / 6.36 | 0.119 / 0.069 |
| `comment-large` | 1.71 / 1.19 | 2.3 / 6.2 | 25.30 / 7.19 | 14.78 / 6.06 | 0.115 / 0.068 |

IPC goes the wrong way for the AVX-512 lexer on all five corpora, and the
branch-miss *rate* is worse on four of five. The rate is per branch, not per
byte: the shipped lexer retires 2.7-4.6x fewer branches per byte, so it takes
0.061-0.070 mispredicts per input byte against flex's 0.115-0.200, fewer on
every corpus. Mispredicts per byte do not order the lexers either: the Scalar
ablation takes 0.118 per byte on `code-large`, the same as flex (0.119), and
runs 2.3x faster than it. The wide instructions are limited by vector
port throughput, while the DFA's per-character transitions predict well.

The difference is instructions per byte: 0.28x to 0.53x of flex, geometric mean
0.38x. Branches per retired instruction drop from about 0.20 to about 0.15, and
what's left is the irregular work at token boundaries.

Cycles per byte implies 1.68x / 1.87x / 2.15x / 2.35x / 2.44x against wall-clock
1.69x / 2.03x / 2.23x / 2.16x / 2.47x. The two disagree by at most 9%.

These per-byte figures are only valid at fixed iteration counts scaled so each run
sees ~3 GB. A single fixed count applied to all corpora leaves the 115 kB corpus
dominated by start-up and inflates its numbers, which is why the `real-src` row
looks odd if you reproduce it with, say, `--benchmark_min_time=400x`.

```sh
cd <build> && taskset -c 2 perf stat -e instructions,cycles,branch-misses,branches \
  ./lexer_bench --no-verify --benchmark_filter='Monkey/Lex/code-large' --benchmark_min_time=713x
```

`--no-verify` is needed because verification runs the reference scanner once,
and those branches would land in the counts. The iteration count is
`ceil(3e9 / corpus_bytes)`: 713 for `code-large`, 611 for `real-libstdcxx`,
25,461 for `real-src`. Using one count for every corpus works for the 4 MiB
inputs and quietly breaks the small ones.

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
./benchmark/paired_mapping.sh 12 2    # paired Monkey/Copy sampling, both shapes
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
| `--external-bytes N` | fallback only: how much libstdc++ to gather when the vendored `real-libstdcxx.txt` is absent, 0 skips it |
| `--quiet` | only print problems |

## What gets measured

| benchmark | includes |
| --- | --- |
| `Monkey/Lex/*`, `Flex/Lex/*` | tokenising only; mapping, token buffer and scanner state are built once |
| `Monkey/Pipeline/*`, `Flex/Pipeline/*` | `open` + `mmap` + `madvise` + tokenise + `munmap`, the per-file cost in a real compile |

## Corpora

`corpus/corpus.hpp` generates them from a fixed seed and only rewrites a file
when the bytes actually change; the external corpus is read from the vendored
`corpus/real-libstdcxx.txt` when it exists.

| corpus | what it is |
| --- | --- |
| `edge` | hand written pathological input: keyword prefixes, `**/`, `/*/*`, glued constants, unsupported bytes, an unterminated block comment, embedded NUL bytes. Verified, never timed, at 1.3 kB |
| `real-src` | this repository's `src/*.cppm` concatenated: 28% whitespace, 0% comments, 72% code |
| `real-libstdcxx` | vendored snapshot of libstdc++ headers (`bits/*.h` from GCC 16), concatenated in sorted order: `corpus/real-libstdcxx.txt`, 4,916,333 bytes; 15% whitespace, 30% comments (comment-dense, not comment-light) |
| `code-small` / `code-large` | generated code: 59%/65% whitespace, ~3% comments, 256 KiB and 4 MiB |
| `comment-large` | generated: 46% whitespace plus 24% comments, 70% skippable bytes in total, 4 MiB |

Composition matters when reading the speedups: skippable bytes (whitespace
outside strings and comments, plus comment bytes) are 28% / 45% / 62% / 68% /
70% of the five inputs against speedups of 1.69x / 2.03x / 2.23x / 2.16x /
2.47x, and the only inversion (code-small 62% skippable but a larger speedup
than code-large's 68%) is 3%, inside cell intervals up to ±10%. Whitespace runs
in code are short — 64-81% of runs are a single byte — so the masks skip most
value in the long tail: 99th-percentile runs of 163-199 bytes on the generated
corpora.

`real-libstdcxx` is the only corpus that is not built from this repository, and
it is the one to quote if you care about external code. It is vendored as a
single file with SHA-256
`5c41477f413c8ef73872ad9409c3a301d198c4a7d0739977aff1109ceb0706f0`,
so its bytes are identical across machines and builds: 4,916,333 bytes and
616,770 tokens. The generator prefers that file (via a compile-time
`MONKEY_PINNED_CORPUS_DIR` define set in `CMakeLists.txt`) and only falls back
to assembling from `/usr/include/c++` when it is absent, in which case the bytes
depend on the installed GCC. On a machine with no vendored file and no headers
under `/usr/include/c++` generation fails rather than silently skipping it; pass
`--external-bytes 0` to opt out.

Roughly 0.4% of those bytes land on the lexer's error path, mostly `.` for member
access and the occasional `[` or `]`, since the token set stops at the operators
Monkey understands. That is low enough that it measures lexing rather than error
recovery, and flex reports the same tokens for those bytes.

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
  ablate/lexer_scalar.cppm       ablation: padded mapping, byte-at-a-time loops, own scalar predicates
  ablate/lexer_masks.cppm        ablation: 512-bit whitespace and comment masks only
  ablate/lexer_identscan.cppm    ablation: vectorized ident/const scan, scalar keyword matching
  ablate/lexer_kwonly.cppm       ablation: scalar scan, packed keyword table
  ablate/lexer_idents.cppm       ablation: vectorized ident/const scan + packed keywords, scalar whitespace
  ablate/lexer_flat.cppm         ablation: per-window cached alnum/digit masks, vectorized whitespace, scalar idents
  ablate/lexer_flatidents.cppm   ablation: flat window cache + vectorized idents, scalar whitespace
  ablate/lexer_copy.cppm         ablation: padded heap buffer via read, no mapping
  corpus/corpus.hpp              deterministic corpus generation, prefers the vendored external corpus
  corpus/real-libstdcxx.txt      vendored libstdc++ headers, 4.7 MiB, sha256 in Corpora above
  bench/bench_lexer.cpp          verification + Google Benchmark harness
  tools/compare.py               pairs the two JSON results into one table
  tools/throughput_chart.py      throughput chart, PDF and PNG
  run.sh                         configure, build, verify, measure
  paired_mapping.sh              paired Monkey/Copy sampling for the mapping ablation
  research.lex                   LaTeX draft, takes its numbers from here
  CMakeLists.txt                 standalone project, also add_subdirectory()able
```

Token codes live in exactly one place, `include/monkey_token_types.h`, and
`bench/bench_lexer.cpp` asserts every entry against the real
`token::TokenType`. Renumber `src/token.cppm` and the benchmark build stops.

The harness compiles `src/token.cppm`, `src/ascii.cppm` and `src/lexer.cppm` as
they are, no copies. The files under `ablate/` are the only copies, and they
exist to be ablations: each one is the shipped lexer with a named technique
removed, so that "how much does the vectorisation actually buy" can be answered
instead of asserted. They live in their own modules and their own namespaces
(`monkey::ablate::scalar`, `::masks`, `::identscan`, `::kwonly`, `::idents`,
`::flat`, `::flatidents`, `::copy`), are registered as `Scalar/Lex/*`,
`Masks/Lex/*`, `IdentScan/Lex/*`, `KwOnly/Lex/*`, `Idents/Lex/*`, `Flat/Lex/*`,
`FlatIdents/Lex/*` and `Copy/Lex/*`, and `--verify-only`
compares them against the shipped lexer's token stream as well as against flex.
If they ever drift from `src/lexer.cppm` the verification fails before anything is
timed.

