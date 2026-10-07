# Lexer benchmarks: `src/lexer.cppm` vs flex

`src/lexer.cppm` is a hand written AVX-512 lexer. This directory times it
against a flex implementation of the same language, so the numbers have
something to compare against that isn't just the same code run twice.

Both lexers run over the same bytes and every token is compared: type, text and
line number. If they disagree the harness prints the mismatches and refuses to
measure anything. The token structs are `static_assert`ed to be the same size
and layout as `token::Token`, and both sides use the same padded mapping size
and token-capacity heuristic (`size / 3 + 16`). The mapping permissions differ:
Monkey uses a read-only file overlay, while flex needs a private writable
overlay for its scanner and NUL patching. In `Lex` the mapping is created
outside the repeated scan; in `Pipeline` flex pays a writable
`MAP_POPULATE` copy-on-write cost on every iteration.

flex reads the mapping directly through `yy_scan_buffer()` rather than
`stdio`, so it isn't charged a `memcpy` that the hand written lexer avoids.

## Results

Intel Core i7-11370H (AVX-512, 4 cores / 8 threads), GCC 16.2,
`-O3 -march=native`, Release, flex 2.6.4 with `--full`. Medians of 7
repetitions at `--benchmark_min_time=1s`, pinned to core 2. Ratio is flex time /
monkey time. These are the numbers in Table 1 of `research.lex` and in
`tools/throughput_chart.py`. Pipeline columns are historical results from a
harness that creates and first-touches a new token buffer each iteration; they
are provisional and are not used as headline evidence:

| corpus | size | Monkey/Lex | Flex/Lex | ratio | Monkey/Pipeline | Flex/Pipeline | ratio |
|---|---|---:|---:|---:|---:|---:|---:|
| real-src | 115 kB | 351.1 MiB/s | 208.3 MiB/s | 1.69x | 351.2 MiB/s | 207.5 MiB/s | 1.69x |
| real-libstdcxx | 4801 kB | 520.2 MiB/s | 257.4 MiB/s | 2.03x | 346.9 MiB/s | 208.6 MiB/s | 1.66x |
| code-small | 260 kB | 592.2 MiB/s | 266.0 MiB/s | 2.23x | 602.1 MiB/s | 259.5 MiB/s | 2.32x |
| code-large | 4114 kB | 599.0 MiB/s | 276.9 MiB/s | 2.16x | 386.7 MiB/s | 206.5 MiB/s | 1.87x |
| comment-large | 4098 kB | 711.6 MiB/s | 288.4 MiB/s | 2.47x | 435.4 MiB/s | 204.6 MiB/s | 2.13x |

Ratios are computed from unrounded medians, so they can differ from the ratio
of the printed columns in the last digit. Lex speedup is a geometric mean of
2.10x. The historical Pipeline geometric mean is 1.92x but is provisional and
not used as a headline. Throughput is `bytes_per_second` from Google Benchmark.
Token counts per run are in `tokens_per_run`.

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

The two real corpora have a 1.85x geometric-mean speedup over flex in the
Lex shape. The five-corpus mean of 2.10x includes three generated corpora.
Neither the input ordering nor the historical Pipeline ratios identify a
mechanism: token density, whitespace/comment share, buffer allocation and
first-touch are confounded.

## What the speedup is actually made of

On `code-large`, the shipped lexer retires 7.83 instructions per byte where the
related scalar ablation retires 14.94, but the cycle rates are close. These
numbers do not identify why the hand-written scanner outperforms the tested
flex configuration. Medians of 7 repetitions at
`--benchmark_min_time=1s`, MiB/s:

| corpus | Scalar | Masks | KwOnly | IdentScan | Idents | Flat | FlatIdents | Monkey | Flex |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `real-src` | 353.5 | 268.6 | 367.8 | 405.4 | 432.1 | 369.2 | 448.3 | 351.1 | 208.3 |
| `real-libstdcxx` | 515.6 | 439.5 | 520.6 | 577.8 | 604.0 | 543.1 | 573.6 | 520.2 | 257.4 |
| `code-small` | 543.5 | 508.5 | 482.4 | 561.6 | 593.8 | 619.8 | 608.9 | 592.2 | 266.0 |
| `code-large` | 639.8 | 593.8 | 603.2 | 652.8 | 657.7 | 698.2 | 700.7 | 599.0 | 276.9 |
| `comment-large` | 678.4 | 642.3 | 643.8 | 640.4 | 737.7 | 777.5 | 746.5 | 711.6 | 288.4 |

`Scalar` is the same lexer with every 64-byte loop replaced by a byte-at-a-time
loop; its 0.99x geometric mean is near parity with this related variant, not an
independently tuned scalar baseline. The split cells take the difference apart:
`IdentScan` vectorizes the identifier/constant scan but keeps keyword matching
scalar (1.05x of `Scalar`), `KwOnly` keeps the scalar scan and adds the packed
keyword table (0.96x of `Scalar`), and `Idents` has both (1.10x of `Monkey`).
`FlatIdents` ranges from 1.07x to 1.14x of `Monkey` across methods, so the
session's 1.12x is not a stable point estimate.

`research.lex` reports 95% within-session intervals (±0.5-9.9% per corpus) and
existing interleaved paired runs for selected cells. The 60 outcomes pool five
corpora and are descriptive, not independent trials; the run order was not
randomized and the full ladder was not paired.

Instructions per byte fall by 48% from Scalar to Monkey on `code-large` (14.94
to 7.83), while cycles per byte change only from 6.85 to 6.36. That mismatch
does not identify a cause. The earlier `perf stat -M TopdownL1` run is withdrawn:
its reported Scalar retiring share (9.2%) implies only about 0.46 retiring slots
per cycle on this five-wide core, inconsistent with the independently measured
IPC of 2.18. None of its four-way percentages are used to support a performance
mechanism. A replacement measurement needs raw event counts over the same scan
window, per-core accounting, and an idle SMT sibling; that controlled setup was
not available for this rerun.

The fixed-iteration per-byte counters are:

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

The results do not establish that scan-loop shape causes the flex difference:
token emission, classification and generator/runtime overhead were not
separated. `Masks` is slower than `Scalar` in these tests, but this is the
naively chunked whitespace/comment-mask implementation, not a general result
about SIMD. Short whitespace runs motivate a scalar fast path before vector
scanning; no such hybrid variant was measured.

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
are medians of the per-pair ratios. Pooled over all 60 outcomes the median ratio
is 0.99x on Lex, where `Copy` wins 20, and 0.97x on `Pipeline`, where it wins
17 (2 of 36 on the three multi-megabyte inputs). These counts are descriptive
across five corpora, not independent-trial significance tests. The counters are
the cleaner evidence for `Lex`: `Copy` and `Monkey` retire identical instructions
per byte, 12.37 on `real-libstdcxx` and 7.83 on `code-large`, with cycles per
byte within 3% (8.28 against 8.08, 6.43 against 6.49). Page faults agree too:
every run took zero major faults, and user-mode minor faults per pipeline
iteration match between the two within 1.3% on all three multi-megabyte corpora
(3,592 against 3,583 on `code-large`, 5,429 against 5,497 on
`real-libstdcxx`, 2,948 against 2,928 on `comment-large`), so fault behaviour
does not separate them either. Their magnitude is consistent with token-buffer
first-touch: on `code-large`, 418,308 emitted 32-byte tokens occupy 13.4 MB, or
about 3,268 4 KiB pages, close to the 3,592 minor faults. The historical
~1 us/fault rate makes those faults about 3.6 ms, while the measured mapping
This points to output-buffer first-touch as a major part of Monkey's pipeline
delta, but does not isolate it experimentally. The buffer allocates one element
per three input bytes, crossing the allocator's 32 MiB mmap threshold only on
multi-megabyte inputs, and the small corpora amortize the run's ~14,000 setup
faults over tens of thousands of iterations. The Copy experiment compares two
read-only mappings; flex uses a writable private mapping and pays additional
copy-on-write costs, measured separately below. On a warm page cache both
Monkey and Copy read the same data; the comparison says nothing about cold-cache
workloads. The padding itself remains required by every variant to omit
load-safety checks near EOF.

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

The historical `Pipeline` shape includes `open`, `mmap`, `madvise`, tokenization,
`munmap`, and a fresh token-buffer allocation on each iteration. Its results
are end-to-end but cannot attribute overhead to mapping. Milliseconds per file;
Lex columns are from the seven-repetition session above, Pipeline columns are
historical medians of five:

| corpus | Monkey Lex | Monkey Pipeline | Flex Lex | Flex Pipeline | Lex ratio | Pipeline ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `real-src` | 0.321 | 0.321 | 0.541 | 0.543 | 1.69x | 1.69x |
| `real-libstdcxx` | 9.039 | 13.557 | 18.306 | 22.548 | 2.03x | 1.66x |
| `code-small` | 0.430 | 0.423 | 0.958 | 0.982 | 2.23x | 2.32x |
| `code-large` | 6.738 | 10.448 | 14.548 | 19.538 | 2.16x | 1.87x |
| `comment-large` | 5.637 | 9.251 | 13.910 | 19.666 | 2.47x | 2.13x |

The read-only mapping-stage probe is documented in Table A.5 of
`research.lex`. A follow-up run of `tools/mapping_syscall_bench.cpp` measured a
55.0 us median and 40 minor faults for the read-only `MAP_POPULATE` overlay,
versus 758.1 us and 1,029 minor faults for the writable overlay. The file has
1,029 pages; an explicit write pass after the writable map took 6.5 us and
caused no further faults, showing that `MAP_POPULATE` pays the copy-on-write
faults during `mmap`. Writable `munmap` took 90.6 us versus 17.0 us for the
read-only mapping. These session-variable micro-timings suggest that writable
population adds about 0.70 ms and unmapping about 0.07 ms; they explain part,
but not all, of the roughly 1.3 ms flex-minus-Monkey Pipeline delta on
`code-large`. An end-to-end run with matched mapping permissions is still
needed.

Reproduce the syscall measurements with:

```sh
c++ -O2 -std=c++20 benchmark/tools/mapping_syscall_bench.cpp -o /tmp/mapping_syscall_bench
taskset -c 2 /tmp/mapping_syscall_bench build/bench/corpus/code-large.txt 1000
```

Run-to-run coefficients of variation in the historical session were 0.7-4.3%
for the shipped lexer's pipeline medians and up to 6.5% for flex. A paired rerun of flex
against Monkey (twelve interleaved launches per corpus at
`--benchmark_min_time=0.3s`) gives 1.68x / 1.65x / 2.30x / 1.97x / 2.09x per
corpus, a 1.92x pooled geometric mean, and flex wins none of the 60 pairs.
Four of the five rows agree with the table above to within 2%; `code-large`
differs by 5% (1.87x against 1.97x), within session drift. The pooled win count
is descriptive across corpora, not an independent-trial significance test.

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
runs 2.3x faster than it. These aggregate counters do not show where the misses
occur or establish why fewer instructions fail to reduce time proportionally.
On `real-libstdcxx`, the rates correspond to about 0.55 misses per Monkey token
and 1.0 per Flex token; sampled `br_misp_retired.all_branches` profiles with
annotation are needed to locate them.

The difference is instructions per byte: 0.28x to 0.53x of flex, geometric mean
0.38x. Branches per retired instruction drop from about 0.20 to about 0.15.
These aggregate results do not identify a cost mechanism. The controlled 3x3
grid below varies identifier length and whitespace-run length independently;
the next useful controls are a scalar-first hybrid whitespace path and a
null-consumer scan.

The current token-type counts can be reproduced with
`./build/bench/lexer_bench --quiet --token-mix`. Percentages exclude the
terminal EOF token; unsupported source constructs appear as error tokens rather
than being silently folded into another class:

| corpus | identifier | constant | keyword | operator/punctuation | error token |
| --- | ---: | ---: | ---: | ---: | ---: |
| `real-src` | 34.2% | 0.9% | 3.6% | 52.4% | 8.9% |
| `real-libstdcxx` | 41.0% | 1.0% | 3.0% | 50.5% | 4.5% |
| `code-small` | 16.4% | 15.1% | 11.1% | 57.4% | 0.0% |
| `code-large` | 16.3% | 15.4% | 11.0% | 57.3% | 0.0% |
| `comment-large` | 16.5% | 15.2% | 11.0% | 57.3% | 0.0% |

`Error` tokens are one byte per unsupported character in both scanners; a
digit-plus-identifier sequence instead becomes one error token spanning the
whole malformed sequence. Bytes inside quoted strings are still tokenized
normally. The two real inputs include 8.9% and 4.5% Error tokens, respectively.
Replacing their spans with spaces (same file byte count) and rerunning seven
timing repetitions on the source snapshot from the paper's recorded commit gives
median-CPU-time speedups of 1.83x on `real-src` and 1.91x on `real-libstdcxx`
(1.87x geometric mean, compared with the original 1.85x). This is a diagnostic
input edit, not a grammar extension.

Recreate that diagnostic using a source tree checked out at the paper's commit:

```sh
./build/bench/lexer_bench --src-dir=/path/to/fda7b59/src --filter-errors --verify-only
./build/bench/lexer_bench --src-dir=/path/to/fda7b59/src --filter-errors --no-verify \
  --benchmark_filter='(Monkey|Flex)/Lex/.*-no-errors' --benchmark_min_time=1s \
  --benchmark_repetitions=7 --benchmark_report_aggregates_only=true
```

The five existing corpora also show an exploratory scaling pattern: bytes per
token rise from 4.1 to 12.1 while counter speedup rises from 1.68x to 2.44x.
The manuscript's `cost-scaling` table combines bytes/token, Flex cycles/byte,
Monkey cycles/token and counter speedup. It is suggestive, not causal, because
token mix and corpus type are confounded.

The controlled 3x3 corpus generator varies identifier length (4/16/64 bytes)
and target mean whitespace-run length (1/8/32 spaces) at 4 MiB fixed size.
The randomized mode uses deterministic random identifier characters and
shuffled run lengths (1--15 for mean 8, 1--63 for mean 32); the one-space
separator cannot vary while remaining positive and averaging one:

```sh
python3 benchmark/tools/control_corpus_grid.py build/bench/control-grid-random \
  --seed 20261007
```

Each generated file can be added with a repeated `--corpus-file=NAME=PATH`
option to `lexer_bench`; use fixed iteration counts with `perf stat` for both
`Monkey/Lex/NAME` and `Flex/Lex/NAME`. The paper reports three shuffled-order
rounds of 100 scans for each of the 18 scanner/input pairs. It also retains the
earlier repeated-identifier grid for comparison. These exploratory samples
still do not establish a pure per-byte/per-token cost model; CPU scaling was
enabled and the SMT sibling was not verified idle. The 54 raw counter rows are
preserved in `benchmark/data/control_grid_random_20261007.csv`. A further
three-round run with `cycles,branch-misses` is preserved in
`benchmark/data/control_grid_branch_misses_20261007.csv`; the paper reports the
median misses/token from these rounds alongside cycles/byte, cycles/token, and
the Flex/Monkey ratio. The cycles/token estimates are derived from the earlier
randomized-grid cycle runs, not these separate branch-counter runs. Perf counts
include benchmark-process startup and harness execution; startup misses were
not subtracted. Treat the roughly 0.0017 misses/byte repeated across Monkey
cells, and the 16-byte/one-space Flex cell, as a measurement floor (<0.002),
not as an exact zero-like result.

Regenerate the five-corpus speedup-versus-bytes/token plot with:

```sh
python3 benchmark/tools/cost_scaling_chart.py
```

This writes `benchmark/cost_scaling_chart.png` and `.pdf`; the plotted values
come from `cost-scaling` in `research.lex`.

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
  tools/mapping_syscall_bench.cpp times mapping setup syscalls by stage
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
