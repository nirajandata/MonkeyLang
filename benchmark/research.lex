\documentclass[11pt]{article}
\usepackage[margin=1in,headheight=22pt,headsep=18pt]{geometry}
\usepackage[T1]{fontenc}
\usepackage{lmodern}
\usepackage{amsmath,amssymb}
\usepackage{microtype}
\usepackage{booktabs}
\usepackage[table]{xcolor}
\usepackage{graphicx}
\usepackage{titlesec}
\usepackage{fancyhdr}
\usepackage{abstract}
\usepackage{titling}
\usepackage{etoolbox}
\usepackage{caption}
\usepackage{needspace}
\usepackage{placeins}

\definecolor{Ink}{HTML}{18324B}
\definecolor{Accent}{HTML}{087F8C}
\definecolor{LinkBlue}{HTML}{285EA8}
\definecolor{Muted}{HTML}{586B7C}
\definecolor{Rule}{HTML}{B8C8D3}
\definecolor{TableHeader}{HTML}{E5EFF3}
\definecolor{TableStripe}{HTML}{F4F8FA}

\usepackage[colorlinks=true,urlcolor=LinkBlue,linkcolor=Accent,citecolor=Accent]{hyperref}

\setlength{\parindent}{0pt}
\setlength{\parskip}{0.45\baselineskip}
\newcommand{\code}[1]{\texttt{\detokenize{#1}}}
\newcommand{\flag}[1]{\texttt{-\kern0pt-\detokenize{#1}}}

\titleformat{\section}
  {\Large\bfseries\color{Ink}}
  {\color{Accent}\thesection.}{0.55em}{}
  [\vspace{0.3ex}{\color{Accent}\titlerule[0.8pt]}]
\titleformat{\subsection}
  {\large\bfseries\color{Ink}}
  {\color{Accent}\thesubsection}{0.65em}{}
\titlespacing*{\section}{0pt}{2.2ex plus 1ex minus .2ex}{1.3ex}
\titlespacing*{\subsection}{0pt}{1.8ex plus .8ex minus .2ex}{0.7ex}

\pretitle{\begin{center}\LARGE\bfseries\color{Ink}}
\posttitle{\par\end{center}\vspace{0.25em}}
\preauthor{\begin{center}\large\color{Muted}}
\postauthor{\par\end{center}}


\renewcommand{\abstractnamefont}{\normalfont\small\bfseries\color{Accent}}
\renewcommand{\abstracttextfont}{\small\color{Muted}}
\setlength{\absleftindent}{1em}
\setlength{\absrightindent}{1em}

\captionsetup{font=small,labelfont={bf,color=Accent},textfont={color=Muted},skip=6pt}
\AtBeginEnvironment{tabular}{\rowcolors{2}{TableStripe}{white}}

\pagestyle{fancy}
\fancyhf{}
\fancyhead[L]{\small\scshape\color{Muted}\ifodd\value{page}\nouppercase{\leftmark}\else Dissecting a Vectorized Lexer\fi}
\fancyfoot[C]{\small\color{Muted}\thepage}
\renewcommand{\headrulewidth}{0.45pt}
\renewcommand{\headrule}{\hbox to\headwidth{\color{Rule}\leaders\hrule height \headrulewidth\hfill}}
\fancypagestyle{plain}{%
  \fancyhf{}
  \fancyfoot[C]{\small\color{Muted}\thepage}
  \renewcommand{\headrulewidth}{0pt}
}

\title{Dissecting a Vectorized Lexer}
\author{Navaraj Dhakal\\\texttt{nirajan.data@gmail.com}}
\date{}

\begin{document}
\maketitle

\begin{abstract}
Applying SIMD to lexical analysis significantly reduces retired instructions relative to a bytewise ablation of the same lexer, but this reduction does not translate into a proportional elapsed time improvement. We compare an AVX-512 lexer against \code{flex} and component ablations on real-code and generated inputs. While SIMD reduces instructions per byte by $19\,\%$ on a vendored libstdc++ input and up to $48\,\%$ on generated code, elapsed time reduction against a related bytewise ablation is negligible. Compared to a standard \code{flex} baseline, the vectorized lexer achieves a $1.85\times$ geometric-mean speedup on real-code inputs ($1.87\times$ when error-token spans are replaced with spaces). An exploratory synthetic grid spans $0.73\times$--$7.21\times$ on randomized inputs and $0.81\times$--$8.70\times$ on repeated identifiers, depending on token length and spacing. Further controlled measurements are needed to distinguish per-byte, per-token, and branch-related costs.
\end{abstract}

\section{Introduction}
Current x86-64 processors have vector registers up to 512 bits wide, but many lexers process input through scalar state-machine transitions. Generators such as \code{lex}, \code{flex}, and \code{re2c} emit code whose cost depends on both the generated scanner and the input. Branch prediction is one possible cost, alongside token emission, classification, and memory traffic; this study measures branch-related counters but does not assume in advance which cost dominates.

SIMD has paid off in JSON parsing~\cite{langdale2019,mison} and in general text processing, including XML~\cite{parabix}. Applying it to programming-language lexing is different: tokens have irregular lengths, comment delimiters can straddle chunk boundaries, and keywords must be recognized.

We looked at how much of this work can move from scalar control flow to mask arithmetic. The implementation targets C++26 and AVX-512 hardware. Its main pieces are:
\begin{enumerate}
    \item A memory-mapping layout that overlays the file on a reserved, zero-filled region with \code{MAP_FIXED}, so wide loads near end of file read zeros instead of faulting, and that passes \code{MAP_POPULATE} to ask the kernel to populate the mapping before scanning starts.
    \item Whitespace skipping and line counting with 64-bit AVX-512 masks, trailing-zero counts, and population counts.
    \item Identifier and constant scanning with the same mask-and-count primitives: range tests over 64 bytes find where an alphanumeric run ends, one chunk at a time.
    \item Keyword matching that packs short ASCII identifiers into 64-bit words and compares them against eight keywords per vector operation, using BMI2 for the masking.
\end{enumerate}
We evaluate the combined implementation and ablate its components. SIMD reduces instruction count but not elapsed time relative to the scalar ablation. We compare the scanner against flex and re2c configurations. The main results are in Section~\ref{sec:results}; limitations and follow-up causal tests are in Section~\ref{sec:limitations}.

\section{Related work}
\label{sec:related}
Parabix applies bit-parallel SIMD techniques to XML and general text processing by representing character-class membership as bit streams~\cite{parabix}. The simdjson parser uses structural-character detection and bit manipulation to accelerate JSON parsing~\cite{langdale2019}. Mison targets selective analytical queries over JSON: it builds structural indices and predicts queried-field locations rather than materializing a conventional full token stream~\cite{mison}. Hyperscan is an open-source regular-expression matching library that combines automata with SIMD techniques~\cite{hyperscan}. Ragel generates executable finite-state machines, including scanners, from regular languages~\cite{ragel}. These systems address different tasks from the token-emitting programming-language lexer studied here, but establish relevant approaches to fast text scanning.

Compiler lexers also optimize common character classes: current Clang source uses a scalar whitespace loop and an SSE2 fast path for block-comment scanning~\cite{clanglexer}. The latter is direct precedent for vectorized delimiter search. Our contribution is a comparison of one AVX-512 lexer with generated scanners and component ablations; the tests show that instruction-count savings do not translate directly into throughput on this processor.

\section{Scanner design}
\label{sec:design}
The scanner uses a padded mapping so 64-byte AVX-512 loads remain readable at end of file. For an input of $N$ bytes and page size $P$, it reserves $P\lceil N/P\rceil+P$ zero-filled bytes, then overlays the file pages with \code{MAP_PRIVATE | MAP_FIXED | MAP_POPULATE}. Logical EOF is checked separately; zero padding only makes the load safe~\cite{mmap}. The implementation uses AVX-512 byte comparisons, mask extraction, population counts, trailing-zero counts, and BMI2 bit masking~\cite{intel}.

Whitespace, line-comment, and block-comment handling uses 64-byte byte-class or delimiter masks. Newline masks update line numbers with population counts. Identifier and number extents use range masks and trailing-zero counts; short keywords are packed into 64-bit words and checked eight at a time. The measured set contains $K=16$ keywords, so lookup uses two eight-lane comparisons. Each token stores a 32-bit token type, a \code{std::string_view} into the input, and a 32-bit line number; the aligned \code{Token} record occupies 32 bytes on the measured ABI.

\paragraph{Scope.}
The measured language subset has ASCII identifiers, short reserved words, and non-nesting block comments. Nested comments, non-ASCII identifiers, and malformed inputs are outside the timed corpora; correctness tests cover selected boundary and error cases. The current experiments do not test other architectures or vector widths.

\section{Experimental Methodology}
\label{sec:methodology}
We use Google Benchmark to compare the lexer against related ablations and generated scanners. What follows describes what we actually ran; the harness is in \code{benchmark/} at commit \href{https://github.com/nirajandata/Monkey/tree/fda7b59b2d7c4386f67ccc8af44521f8f51bbb3a}{\code{fda7b59}}.
\begin{itemize}
    \item \textbf{Baselines.} Four generated scanners recognize the same token subset and are verified token-for-token. The reference uses \code{flex}~\cite{flex} 2.6.4 with \code{--full}, equivalent to \code{--Cfr}: full, uncompressed tables with stdio bypassed. In flex's separate short-option notation, \code{-Cf} selects full-table generation, \code{-CF} selects the alternate fast-table representation, and \code{-Cr} bypasses stdio; \code{-CF} is not \code{-Cf}. One variant replaces flex keyword rules with our AVX-512 lookup. The \code{re2c}~\cite{re2c} 4.6 scanner is generated with only \code{--no-debug-info}; it does not use \code{-b} or \code{--computed-gotos}. Its \code{YYFILL} handling is enabled and the harness's \code{YYFILL(n)} returns at input exhaustion. This is an unoptimized measured configuration; comparisons are configuration-specific, not claims about re2c generally. All generated C is compiled with GCC 16.2.1 at \code{-O3 -march=native}.
    \item \textbf{Harness.} Scanners receive the same bytes, token capacity, and direct-buffer interface (\code{yy_scan_buffer} for flex). Both mappings use \code{MAP_POPULATE} and \code{MADV_SEQUENTIAL | MADV_WILLNEED}, but Monkey maps read-only while flex maps private writable input and NUL-patches token boundaries. That distinction is outside the repeated scan in \emph{Lex}, but each \emph{Pipeline} iteration pays the writable mapping's copy-on-write population cost; it does not uniformly favor the baselines.
    \item \textbf{Correctness.} Every scanner is checked against the shipped lexer on every corpus for token type, text, and line number before timing.
    \item \textbf{Token mix.} Appendix Table~\ref{tab:token-mix} reports token classes from the harness's \code{--token-mix} mode, excluding terminal \code{Eof} and reporting \code{Error} tokens separately. The optional \code{--filter-errors} experiment replaces each emitted \code{Error} span with spaces before timing.
    \item \textbf{Corpora.} \code{real-src} concatenates this repository's source files (115\,KiB). \code{real-libstdcxx} is the vendored 4,916,333-byte snapshot in \path{benchmark/corpus/real-libstdcxx.txt}, SHA-256 \code{5c41477f413c8ef73872ad9409c3a301d198c4a7d0739977aff1109ceb0706f0}. Three fixed-seed generated corpora contain code-dense 260\,KiB and 4.0\,MiB inputs and a comment-dense 4.0\,MiB input. Pathological cases are verified but not timed.
    \item \textbf{Ablations.} The ladder compares bytewise \code{Scalar}, whitespace/comment masks (\code{Masks}), vectorized identifier work with scalar whitespace (\code{Idents}), cached-mask variants (\code{Flat}, \code{FlatIdents}), the shipped \code{Monkey}, and flex. \code{IdentScan} and \code{KwOnly} split identifier scanning from keyword lookup; \code{Copy} replaces the mapping with a padded heap buffer. All are token-verified before timing.
    \item \textbf{Hardware and metrics.} Runs use one pinned core of an Intel Core i7-11370H~\cite{intelcpu} with GCC 16.2.1 at \code{-O3 -march=native}. Throughput is decimal GB/s; \code{perf stat} collects user-space IPC, instruction, cycle, and branch-miss counts separately from timing. Exploratory level-1 top-down percentages are omitted: their reported retiring shares conflict with the independently measured IPC and have not been validated with raw per-core events while the SMT sibling is idle.
\end{itemize}

\begin{table}[htbp]
\centering
\caption{Token counts, byte composition, and whitespace-run statistics per corpus. Token counts include the terminal \code{Eof}; Appendix Table~\ref{tab:token-mix} excludes it. \emph{ws} is whitespace outside strings and comments; \emph{comment} counts only block-comment bytes, omitting both \texttt{\#}-started lines (including preprocessor directives) and \code{//} comments; these bytes remain in \emph{code}. Thus the reported comment and skippable-byte shares are lower bounds. A \emph{run} is a maximal run of whitespace encountered while scanning code; lengths are in bytes, and \emph{len1} is the share of runs exactly one byte long.}
\label{tab:composition}
\small
\begin{tabular}{lrrrrrrrrrr}
\toprule
\rowcolor{TableHeader}
& \multicolumn{4}{c}{Corpus summary} & \multicolumn{6}{c}{Whitespace runs in code}\\
\cmidrule(lr){2-5}\cmidrule(lr){6-11}
Corpus & tokens & ws (\%) & comment (\%) & code (\%) & count & avg & p50 & p90 & p99 & len1 (\%)\\
\midrule
\code{real-src}      & 28{,}636 & 28.0 &  0.0 & 72.0 &  9{,}708 &  3.4 & 1 &   9 &  24 & 67\\
\code{real-libstdcxx} & 616{,}770 & 14.8 & 30.5 & 54.6 & 289{,}308 &  2.5 & 1 &   7 &   9 & 64\\
\code{code-small}    & 30{,}890 & 59.4 &  2.9 & 37.7 & 15{,}523 & 10.2 & 1 &  26 & 163 & 77\\
\code{code-large}    & 418{,}308 & 65.0 &  2.5 & 32.5 & 209{,}868 & 13.1 & 1 &  36 & 199 & 77\\
\code{comment-large} & 345{,}815 & 46.5 & 23.6 & 29.9 & 219{,}307 &  8.9 & 1 &  17 & 161 & 81\\
\bottomrule
\end{tabular}
\end{table}

We report two benchmark shapes. \emph{Lex} times tokenization with mapping, token buffer, and scanner state created once. \emph{Pipeline} includes \code{open}, \code{mmap}, \code{madvise}~\cite{madvise}, tokenization, \code{munmap}, and a fresh token-buffer allocation on each iteration. The latter is a provisional end-to-end harness measurement, not a clean measure of mapping overhead; it is not used as a headline result.

The correctness tests cover empty input, page-aligned files, tokens crossing vector boundaries, eight-byte identifiers, longer identifiers that share a keyword prefix, comments ending at EOF or across chunk boundaries, and embedded NUL bytes.

Lex throughput is the median of seven repetitions. The harness consumes the token vector on every iteration and uses equally sized preallocated output buffers; Pipeline has a different allocation protocol described below.

\section{Results and Analysis}
\label{sec:results}
Throughput is the median of seven repetitions at \flag{benchmark_min_time=1s}. Lex is stable enough at this protocol to quote. Pipeline sessions of an unchanged binary moved by as much as $20\,\%$; the pipeline data are provisional and discussed in Section~\ref{sec:pipeline}.

We collected hardware counters in separate runs under \code{perf stat}, pinned to one core with \flag{no-verify}. These runs fix the iteration count instead of a time budget. With a time budget the faster lexer finishes more iterations, and its raw instruction count would understate its per-byte cost. With a fixed count both lexers process the same number of bytes, so instructions can be normalized per byte. We chose the iteration count per corpus so that every counter run handles $3.0$\,GB of input; for the $115$\,KiB corpus that is $25{,}461$ iterations, and a short run would still be dominated by start-up.

Table~\ref{tab:results} gives throughput, speedup, IPC, branch-miss ratio, and branch misses per input byte for each corpus. IPC and miss ratio do not depend on iteration count. The speedup column comes from the timing runs and is cross-checked against cycle counts in Table~\ref{tab:work-per-byte}; Section~\ref{sec:repro} reports within-session confidence intervals and paired outcomes for selected cells.

\begin{table}[htbp]
\centering
\caption{Throughput, IPC, branch-miss ratio, and branch misses per input byte for the shipped lexer against the \code{flex} baseline. Throughput is the median of seven repetitions at \flag{benchmark_min_time=1s}; counters come from separate pinned \code{perf} runs at fixed iteration count and are listed as baseline\,/\,vectorized. Miss per byte is the miss ratio times branches per byte, i.e.\ mispredicts retired per input byte. Speedup is the baseline's median wall-clock time divided by the vectorized lexer's, so it can differ from the ratio of the rounded throughput columns in the last digit. All entries are measured with the harness in \code{benchmark/}.}
\label{tab:results}
\small
\setlength{\tabcolsep}{4pt}
\begin{tabular}{llrrrrrr}
\toprule
\rowcolor{TableHeader}
& & \multicolumn{2}{c}{Throughput (GB/s)} & & \multicolumn{3}{c}{Counters}\\
\cmidrule(lr){3-4}\cmidrule(lr){6-8}
Corpus & Size & flex & AVX-512 & Speedup & IPC & Miss (\%) & Miss/byte\\
 & & \flag{full} & this work & & flex\,/\,avx & flex\,/\,avx & flex\,/\,avx\\
\midrule
\code{real-src}     & 115 KiB & 0.2184 & 0.3682 & $1.69\times$ & 1.85 / 1.65 & 2.9 / 2.4 & 0.200 / 0.061\\
\code{real-libstdcxx} & 4.7 MiB & 0.2699 & 0.5455 & $2.03\times$ & 2.22 / 1.56 & 2.0 / 3.3 & 0.123 / 0.067\\
\code{code-small}   & 260 KiB & 0.2789 & 0.6210 & $2.23\times$ & 1.63 / 1.50 & 2.4 / 3.6 & 0.137 / 0.070\\
\code{code-large}   & 4.0 MiB & 0.2904 & 0.6281 & $2.16\times$ & 1.53 / 1.23 & 2.5 / 6.3 & 0.119 / 0.069\\
\code{comment-large} & 4.0 MiB & 0.3024 & 0.7462 & $2.47\times$ & 1.71 / 1.19 & 2.3 / 6.2 & 0.115 / 0.068\\
\midrule
\multicolumn{4}{l}{Geometric mean speedup} & $2.10\times$ & & & \\
\bottomrule
\end{tabular}
\end{table}

Table~\ref{tab:work-per-byte} restates the counters as work per input byte. The cycle-derived speedups agree with the timed ones.

\begin{table}[htbp]
\centering
\caption{Work per input byte at equal byte counts, for the shipped lexer against the \code{flex} baseline. These are the quantities that favor the vectorized lexer; the IPC and miss-ratio columns of Table~\ref{tab:results} do not. The last column repeats the timed speedup for comparison with the counter-derived one. Counter-derived and timed speedups agree to within $9\,\%$ on every corpus.}
\label{tab:work-per-byte}
\begin{tabular}{lrrrrrrr}
\toprule
\rowcolor{TableHeader}
& \multicolumn{2}{c}{Instructions / byte} & & \multicolumn{2}{c}{Cycles / byte} & Counter & Timed\\
\cmidrule(lr){2-3}\cmidrule(lr){5-6}
Corpus & flex & AVX-512 & Ratio & flex & AVX-512 & speedup & speedup\\
\midrule
\code{real-src}     & 35.99 & 19.10 & $0.53\times$ & 19.43 & 11.57 & $1.68\times$ & $1.69\times$\\
\code{real-libstdcxx} & 32.88 & 12.37 & $0.38\times$ & 14.84 & 7.95 & $1.87\times$ & $2.03\times$\\
\code{code-small}   & 27.22 & 11.62 & $0.43\times$ & 16.66 & 7.76 & $2.15\times$ & $2.23\times$\\
\code{code-large}   & 22.86 & 7.83 & $0.34\times$ & 14.94 & 6.36 & $2.35\times$ & $2.16\times$\\
\code{comment-large} & 25.30 & 7.19 & $0.28\times$ & 14.78 & 6.06 & $2.44\times$ & $2.47\times$\\
\midrule
\multicolumn{3}{l}{Geometric mean, instructions / byte} & $0.38\times$ & & & & \\
\bottomrule
\end{tabular}
\end{table}

\subsection{Baseline variants}
\label{sec:variants}

Table~\ref{tab:results} compares the shipped lexer with the reference flex configuration; Table~\ref{tab:variants} reports the other configurations. The tested re2c configuration is $32\,\%$ slower than flex on \code{real-src}, yet faster on the three generated corpora. Since it was generated without \code{-b} or \code{--computed-gotos} and retained enabled \code{YYFILL} handling, this is a result about our measured configuration, not re2c generally. Across the two real-code corpora the reference flex speedup has geometric mean $1.85\times$; the five-corpus mean of $2.10\times$ includes three synthetic inputs.

\begin{table}[htbp]
\centering
\caption{Throughput in decimal GB/s for the shipped lexer against the four generated baseline configurations. \code{FlexAVXKW} adds the AVX-512 keyword table; \code{FlexCF} uses flex's alternate fast-table representation selected by \code{-CF}; \code{Re2c} uses the configuration described in Section~\ref{sec:methodology}.}
\label{tab:variants}
\begin{tabular}{lrrrrr}
\toprule
\rowcolor{TableHeader}
Corpus & \code{Monkey} & \code{Flex} & \code{FlexAVXKW} & \code{FlexCF} & \code{Re2c}\\
\midrule
\code{real-src}     & 0.3682 & 0.2184 & 0.2083 & 0.2117 & 0.1483\\
\code{real-libstdcxx} & 0.5455 & 0.2699 & 0.2337 & 0.2314 & 0.1949\\
\code{code-small}   & 0.6210 & 0.2789 & 0.2611 & 0.2889 & 0.2906\\
\code{code-large}   & 0.6281 & 0.2904 & 0.2938 & 0.3052 & 0.3306\\
\code{comment-large} & 0.7462 & 0.3024 & 0.3040 & 0.3107 & 0.3856\\
\midrule
Relative to \code{Monkey} & $1.00\times$ & $0.48\times$ & $0.45\times$ & $0.47\times$ & $0.45\times$\\
\bottomrule
\end{tabular}
\end{table}

\subsection{Ablations}
\label{sec:ablation}

The counters in Table~\ref{tab:work-per-byte} say the vectorized lexer retires far fewer instructions per byte, which is what the design predicted. That is not the same as saying the vectorization causes the speedup, and we tested it directly. Each ablated lexer is a copy of the shipped one with one technique removed; the padded mapping is present in every one of them, so the ladder isolates the chunked loops rather than the recipe for reading the file.

Table~\ref{tab:ablation-throughput} reports the ladder. \code{Scalar} is the shipped lexer with each 64-byte loop replaced by a byte-at-a-time loop and keyword lookup replaced by a linear compare; it is not an independently tuned scalar scanner. \code{Masks} retains the naive 512-bit whitespace and comment-mask path but scans identifiers and classifies keywords scalarly. \code{Idents} scans whitespace and comments byte-at-a-time while vectorizing identifier, constant, and keyword work. \code{Flat} computes whitespace, newline, alphanumeric, digit, and asterisk masks once per 64-byte window and reuses them; \code{FlatIdents} uses the cached masks for identifier and constant work with scalar whitespace scanning. \code{Monkey} is the shipped lexer and \code{Flex} the baseline. Table~\ref{tab:ablation-scan-kw} splits the \code{Idents} cell: \code{IdentScan} keeps vectorized identifier and constant scanning but classifies keywords with a linear compare, while \code{KwOnly} scans byte-at-a-time but uses the packed keyword lookup.

\begin{table}[htbp]
\centering
\caption{Ablation ladder, throughput in decimal GB/s. Each row is verified to emit the shipped lexer's exact token stream. The final three rows give five-corpus geometric means normalized to \code{Monkey}, \code{Scalar}, and \code{Flex}, respectively.}
\label{tab:ablation-throughput}
\begin{tabular}{lrrrrrrr}
\toprule
\rowcolor{TableHeader}
Corpus & \code{Scalar} & \code{Masks} & \code{Idents} & \code{Flat} & \code{FlatIdents} & \code{Monkey} & \code{Flex}\\
\midrule
\code{real-src}     & 0.3707 & 0.2816 & 0.4531 & 0.3871 & 0.4701 & 0.3682 & 0.2184\\
\code{real-libstdcxx} & 0.5406 & 0.4608 & 0.6333 & 0.5695 & 0.6015 & 0.5455 & 0.2699\\
\code{code-small}   & 0.5699 & 0.5332 & 0.6226 & 0.6499 & 0.6385 & 0.6210 & 0.2789\\
\code{code-large}   & 0.6709 & 0.6226 & 0.6897 & 0.7321 & 0.7347 & 0.6281 & 0.2904\\
\code{comment-large} & 0.7114 & 0.6735 & 0.7735 & 0.8153 & 0.7828 & 0.7462 & 0.3024\\
\midrule
Relative to \code{Monkey} & $0.99\times$ & $0.87\times$ & $1.10\times$ & $1.08\times$ & $1.12\times$ & $1.00\times$ & $0.48\times$\\
Relative to \code{Scalar} & $1.00\times$ & $0.88\times$ & $1.12\times$ & $1.09\times$ & $1.14\times$ & $1.01\times$ & $0.48\times$\\
Relative to \code{Flex} & $2.07\times$ & $1.82\times$ & $2.31\times$ & $2.26\times$ & $2.35\times$ & $2.10\times$ & $1.00\times$\\
\bottomrule
\end{tabular}
\end{table}

On \code{code-large}, \code{Monkey}'s session throughput ($0.628$\,GB/s) and counter rate ($6.36$ cycles per byte) imply an effective frequency of about $4.0$\,GHz, below the measured sustained median of $4.30$\,GHz. This low denominator can inflate several per-corpus ratios in that row. We therefore include geometric means normalized to \code{Scalar} and \code{Flex} as well as \code{Monkey} in Table~\ref{tab:ablation-throughput}.

The five-corpus geometric means place \code{Scalar} at $0.99\times$ \code{Monkey} in the session, $0.99\times$ in the existing paired runs, and $0.95\times$ in the counter estimate. This is near parity between two variants of the same codebase. \code{Masks}, which retains the naive vector whitespace/comment path but scans identifiers and keywords scalarly, is slower on every corpus. \code{Idents} and \code{FlatIdents} are faster in the main session, but their order changes across methods. Across counter, session, and paired estimates, \code{FlatIdents} ranges from $1.07$ to $1.14\times$ \code{Monkey}. These data show a variable advantage across methods.

Table~\ref{tab:ablation-scan-kw} splits the identifier-side changes. In this implementation the scalar keyword path tests identifier length and compares the token against keyword spellings in sequence; the first exact match returns a keyword, otherwise the path ends as an identifier. The generated-corpus keyword fraction and branch-miss counts per byte for \code{IdentScan} and \code{Idents} were not recorded, so the cycle difference cannot be attributed to keyword frequency or branch prediction. The two cells retire nearly identical instructions per byte ($13.53$ and $13.55$ on \code{code-large}), but \code{Idents} uses fewer cycles per byte ($5.99$ versus $6.57$).

The two-by-two design shows that this implementation's naive chunk-at-a-time whitespace/comment mask path loses in these inputs, while the combined identifier and keyword work can improve throughput. A hybrid that tests the next byte scalarly before entering a vector loop, or a cheaper character-class test, was not measured. This result applies specifically to this chunk-at-a-time mask path.

Table~\ref{tab:ablation-counters} shows the instruction/time mismatch. On \code{code-large}, the scalar ablation retires $14.94$ instructions per byte and takes $6.85$ cycles per byte; the shipped lexer retires $7.83$ instructions and takes $6.36$ cycles. IPC falls from $2.18$ to $1.23$. The \code{Idents} cell retires $13.53$--$13.55$ instructions per byte and takes fewer cycles than either. These measurements establish that fewer instructions do not translate into a proportional time reduction.

One caveat bounds every ladder result in this section. Every lexer here, \code{Copy} included, presents the scan loop with a buffer that is already resident and padded, so the ladder isolates scan code and not the cost of getting the bytes into memory. Section~\ref{sec:mapping} reports the separate, still-provisional end-to-end comparison.

The exploratory top-down run is not used here: its reported retiring share for \code{Scalar} (9.2\,\%) is below the minimum implied by its measured IPC of 2.18 on this five-wide core. The counting scope and SMT effects have not been reconciled, so neither that result nor its other categories support a claim about speculation or port limits. A valid rerun requires raw event counts collected over a matched window with per-core accounting and an idle sibling.

On \code{code-large}, branch misses per byte are $0.069$ for the shipped lexer, $0.118$ for \code{Scalar}, and $0.119$ for flex. Assuming roughly 18 cycles of recovery per miss gives a rough estimate of about one-fifth of \code{Monkey}'s cycles attributable to branch-mispredict recovery. This estimate is not a profile of where misses occur and cannot establish whether branch behavior explains the performance differences. Sampling and annotating the branch-mispredict sites remains necessary.

\begin{table}[htbp]
\centering
\caption{Sampled core frequency under sustained execution. A monitoring thread read \code{scaling_cur_freq} every $20$\,ms for the lifetime of a pinned run of each lexer on \code{code-large} at \flag{benchmark_min_time=8s}. \code{p10} is the 10th percentile, \code{max} the highest sample, and $n$ the sample count. Both lexers sit at the same $4.30$\,GHz median, so the shipped lexer's denser use of 512-bit execution does not pull the clock down on this part.}
\label{tab:frequency}
\begin{tabular}{lrrrr}
\toprule
\rowcolor{TableHeader}
Lexer & Median (GHz) & \code{p10} (GHz) & \code{max} (GHz) & $n$\\
\midrule
\code{Monkey} & 4.30 & 4.25 & 4.65 & 832\\
\code{Flex}   & 4.30 & 4.30 & 4.68 & 590\\
\bottomrule
\end{tabular}
\end{table}

Table~\ref{tab:frequency} shows both lexers at a $4.30$\,GHz median sampled frequency, with a small difference at the 10th percentile. This makes sustained AVX-512 downclocking an unlikely explanation for the cycle gap.

\subsection{Uncertainty and cross-method agreement}
\label{sec:repro}

Three questions bound how much of the ladder to believe: how wide the timing intervals are, whether the ordering survives a protocol that does not depend on session medians, and whether the counter runs measure the same thing the timing runs do. Table~\ref{tab:repro} answers all three side by side.

The intervals are wide enough to change claims from a single session. Per-corpus widths on the ratio to \code{Monkey} run from $\pm0.5\,\%$ to $\pm9.9\,\%$, widest on \code{code-small}, where run-to-run coefficients of variation reach $9\,\%$. The existing paired protocol used twelve strictly interleaved launches per tested variant and corpus, but it tested only the top cells and always used the same variant order. The reported 60 pair outcomes pool five corpora; they are descriptive counts. \code{Idents} wins all sixty recorded outcomes, \code{FlatIdents} wins fifty-two, and \code{Scalar} wins twenty-two. A randomized, paired run of the full ladder is needed for inferential ranking.

The counter column explains why Tables~\ref{tab:ablation-throughput} and~\ref{tab:ablation-counters} do not match exactly, rather than leaving the discrepancy to be waved away. Counter runs are separate processes: measured against each cell's own session median they ran at $0.898$--$1.050\times$ throughput, and their effective clocks spanned $4.01$--$4.25$\,GHz. On \code{code-large} that skew is largest --- the \code{Monkey} counter run went $5\,\%$ fast while the \code{Scalar} and \code{FlatIdents} runs went $5$--$6\,\%$ slow --- and multiplying each session ratio by its run/session factor moves the predicted counter ratio to within $5$ percentage points of the measured one, the remainder tracking the effective clock. Across all cells the session and counter estimates differ by at most $5$ points on geometric mean, with identical signs and identical ordering except for the top two, so the counter runs confirm the ladder rather than reproduce its exact ratios.

The session confidence intervals treat seven repetitions as independent and describe within-session sampling only; they do not capture session-to-session drift. In particular, \code{Scalar} moved by about eight percentage points between sessions; it is within $1.5\,\%$ of \code{Monkey} in the paired estimate ($0.99\times$), while the counter estimate is $0.95\times$. The paired count is not interpreted as a significance test. For \code{FlatIdents}, session, paired, and counter estimates span $1.07$--$1.14\times$ \code{Monkey}, with no stable ranking over \code{Idents}. We use these estimates as a range, not as a precise point claim.

\subsection{Isolating the padded mapping}
\label{sec:mapping}

The ablations above all inherit the padded mapping, which leaves the recipe for reading the file unmeasured. To separate it we added \code{Copy}: the shipped lexer with its scan loops byte-for-byte unchanged, reading the file into a heap buffer instead of mapping it. The buffer is allocated with the same page-aligned, zero-padded geometry as the mapping, so the two variants differ in exactly one respect, how the bytes arrive: \code{Monkey} uses the reservation plus \code{MAP_FIXED | MAP_POPULATE} and \code{madvise}, while \code{Copy} issues \code{read} calls into an \code{aligned_alloc} allocation zero-filled only in the padding, exactly the geometry the mapping provides. The scan loop cannot tell them apart, which is the point: any difference is attributable to the mapping.

Measuring this pair needs a different protocol from Section~\ref{sec:results}, and getting that wrong is instructive. Whole-session seven-repetition medians are unstable for the \emph{Pipeline} shape: two sessions of the same binary put \code{Copy} at $0.94\times$ and $1.01\times$ of \code{Monkey}, with corpus rows moving by as much as $20\,\%$. Each iteration re-creates the lexer, mapping, and token buffer while page-cache and frequency state drift. We therefore use twelve strictly interleaved process pairs per corpus at \flag{benchmark_min_time=0.3s}, and report the median of per-pair throughput ratios.

Table~\ref{tab:mapping} gives the result. In the \emph{Lex} shape, \code{Copy} is about $1\,\%$ slower than \code{Monkey} (pooled paired ratio $0.99\times$); the pooled count is twenty wins in sixty outcomes across five corpora. The count is descriptive. Instructions per byte are identical on both profiled corpora, and cycles per byte differ by at most $3\,\%$. Page-fault counts also agree closely: each run had zero major faults, and minor faults per pipeline iteration differ by at most $1.3\,\%$ on the three multi-megabyte inputs.

\begin{table}[htbp]
\centering
\caption{The padded mapping against a heap copy, from twelve interleaved paired samples per corpus. Lex throughput is in decimal GB/s; ratios are medians of per-pair ratios. Counter columns are per byte.}
\label{tab:mapping}
\begin{tabular}{lrrrrrrr}
\toprule
\rowcolor{TableHeader}
& \multicolumn{2}{c}{\emph{Lex} (GB/s)} & \multicolumn{2}{c}{Instructions / byte} &
\multicolumn{2}{c}{Cycles / byte} & Pipeline\\
\cmidrule(lr){2-3}\cmidrule(lr){4-5}\cmidrule(lr){6-7}
Corpus & \code{Copy} & Ratio & \code{Copy} & \code{Monkey} & \code{Copy} & \code{Monkey} & Ratio\\
\midrule
\code{real-src}     & 0.378 & $0.99\times$ & & & & & $1.00\times$\\
\code{real-libstdcxx} & 0.546 & $0.98\times$ & 12.37 & 12.37 & 8.28 & 8.08 & $0.94\times$\\
\code{code-small}   & 0.598 & $1.00\times$ & & & & & $1.00\times$\\
\code{code-large}   & 0.676 & $1.00\times$ & 7.83 & 7.83 & 6.43 & 6.49 & $0.96\times$\\
\code{comment-large} & 0.727 & $0.99\times$ & & & & & $0.95\times$\\
\midrule
\multicolumn{2}{l}{Median, pooled over 60 pairs} & $0.99\times$ & \multicolumn{2}{r}{identical} & \multicolumn{2}{r}{within $3\,\%$} & $0.97\times$\\
\bottomrule
\end{tabular}
\end{table}

The \emph{Pipeline} comparison is end-to-end, but the current harness allocates a fresh token buffer inside each iteration. The observed $0.97\times$ pooled \code{Copy} ratio therefore does not isolate mapping from token-buffer allocation, first-touch faults, and scanning. The read-only mapping stages in Table~\ref{tab:map-syscalls} account for only a small part of the \code{Monkey} pipeline delta; flex's writable private mapping has a separate copy-on-write cost measured in Table~\ref{tab:map-writable}. A reusable-buffer end-to-end comparison with matched mapping permissions is still needed to isolate the remaining costs.

The \emph{Lex} comparison is a warm-cache result and does not measure the padding's standalone benefit. Every ablation uses the same padded buffer, so this experiment cannot quantify whether padding improves scan-loop performance.

\subsection{Pipeline shape}
\label{sec:pipeline}

The original \emph{Pipeline} results imply about $3.7$\,ms of added time for \code{Monkey} and $4.9$\,ms for flex on \code{code-large}; the flex-minus-Monkey difference is about $1.3$\,ms. The \code{Monkey} run reported $3{,}592$ minor faults per iteration, or roughly $1\,\mu\mathrm{s}$ per fault. The harness creates a new lexer and token vector each iteration, so allocation and token-buffer first-touch are included. At 418,308 emitted tokens and 32 bytes per token, the written token records occupy about 13.4\,MB, or 3,268 4\,KiB pages; this is close to the observed fault count and is consistent with output-buffer first-touch as the dominant source of \code{Monkey}'s pipeline penalty.

Mapping is asymmetric between the scanners. \code{Monkey} overlays the file read-only, whereas flex uses \code{PROT_READ | PROT_WRITE} with \code{MAP_PRIVATE | MAP_POPULATE}. In a separate 1,000-iteration mapping probe, the read-only \code{MAP_POPULATE} overlay took a $55.0\,\mu\mathrm{s}$ median and incurred 40 minor faults; the writable overlay took $758.1\,\mu\mathrm{s}$ and incurred 1,029 faults, one per mapped page. An explicit write pass after the writable mapping took $6.5\,\mu\mathrm{s}$ and incurred no further faults, confirming that \code{MAP_POPULATE} had already paid the copy-on-write cost. This adds about $0.70$\,ms over the read-only overlay. Writable \code{munmap} took $90.6\,\mu\mathrm{s}$ versus $17.0\,\mu\mathrm{s}$ for the read-only mapping, a further $0.07$\,ms rather than the remainder of the roughly $1.3$\,ms observed pipeline difference. Table~\ref{tab:map-syscalls} reports read-only setup-stage timings; Appendix Table~\ref{tab:map-writable} isolates the permission comparison. These separate syscall tests do not replace an end-to-end run with both mapping permissions controlled, and the remaining pipeline gap is unresolved. The micro-timings vary substantially between sessions: the read-only overlay shifted from $77.8$ to $53.5$--$55.0\,\mu\mathrm{s}$ and its \code{munmap} from $34.3$ to about $17\,\mu\mathrm{s}$ across runs; treat them as scale estimates, not precise pipeline attribution.

\subsection{What the external corpus represents}
\label{sec:external}

Four of the five corpora are generated. The external sample is a vendored concatenation of libstdc++ \code{bits} headers: $4{,}916{,}333$ bytes, $616{,}770$ tokens, and $152{,}018$ lines. It is heavily templated, concatenated into one file, contains no non-ASCII identifiers, and is not a broad sample of codebases. The composition table counts only block comments; its $30.5\,\%$ figure omits both \code{//} comments and \texttt{\#}-started lines, including preprocessor directives, and is a lower bound on skipped comment bytes. The generated inputs contain only tokenizable text. These corpus choices limit generalization.

On this corpus, the scalar and shipped lexers are within about $1\,\%$ in the session data, and the flex speedup is $2.03\times$. The real-corpus headline remains the two-corpus geometric mean, not this single corpus or the synthetic-input mean.

Because the corpus is vendored as a single file with a recorded SHA-256 (Section~\ref{sec:methodology}), its bytes are identical across machines and across builds: the measurement is bit-reproducible in a way an assembled-from-the-toolchain corpus would not be. The fallback path that reassembles from the local toolchain exists only to keep the build working if the snapshot is removed, and is not what any number in this paper was measured with.

Figure~\ref{fig:throughput} plots the throughput columns of Tables~\ref{tab:ablation-throughput} and~\ref{tab:variants}.

\begin{figure}[htbp]
\centering
\includegraphics[width=\linewidth]{throughput_chart.png}
\caption{Lexing throughput in decimal GB/s over all five corpora. Left: the ablation ladder of Table~\ref{tab:ablation-throughput}. Right: the shipped lexer against the four generated baseline configurations of Table~\ref{tab:variants}. Our lexers are dark hatched fills and the baselines light solid fills, so the chart reads correctly in black and white. The external corpus, \code{real-libstdcxx}, is the one a compiler front end would most resemble.}
\label{fig:throughput}
\end{figure}

\subsection{Analysis}
\label{sec:analysis}
The real-libstdc++ corpus has $616{,}770$ tokens in $4{,}916{,}333$ bytes, or about eight input bytes per token. Table~\ref{tab:cycle-per-token} reports fixed-counter-derived cycles per token for four scanners where counters are available. On the external corpus the estimates are about $63$ cycles per token for \code{Monkey} and $118$ for flex, which makes token emission and classification plausible contributors but does not measure their individual cost. Appendix Table~\ref{tab:token-mix} reports the token-type mix: generated corpora are about $57\,\%$ operator/punctuation tokens, while the real corpora contain error tokens because this scanner recognizes only a subset of C++. Unsupported characters produce one error token per byte in both scanners; malformed digit-plus-identifier sequences instead produce one error token spanning the full sequence. For example, quotes, periods, and square brackets are unsupported bytes; \code{->} is two supported operator tokens, and \code{::} is two supported colon tokens. Bytes inside quoted strings are still lexed as ordinary tokens. Replacing error-token spans with spaces and rerunning on the exact paper source snapshot gives median-CPU-time speedups of $1.83\times$ on \code{real-src} and $1.91\times$ on \code{real-libstdcxx}, a $1.87\times$ geometric mean versus the original $1.85\times$. In these filtered files, raw whitespace runs changed from $10{,}026$ to $11{,}614$ on \code{real-src} (mean length $3.32$ to $3.09$ bytes; one-byte share $68.5\,\%$ to $69.5\,\%$) and from $513{,}421$ to $523{,}154$ on \code{real-libstdcxx} (mean $2.16$ to $2.22$; one-byte share $71.1\,\%$ to $70.0\,\%$). These bytewise statistics include whitespace inside comments and strings; the diagnostic preserves file size but changes input structure and is not equivalent to implementing the missing grammar. A null-consumer scan that counts without storing tokens is still needed to isolate emission.

The original five corpora also suggest an organizing hypothesis: \code{Monkey}'s counter-derived cost is steadier per emitted token, while flex's is steadier per input byte, so the ratio rises with bytes per token. Table~\ref{tab:cost-scaling} puts the quantities together. The counter speedup is monotonic across these five rows; the timed speedup has one inversion, with \code{code-small} at $2.23\times$ and \code{code-large} at $2.16\times$. Input composition and token type remain confounded, so this trend alone is not evidence of a per-token versus per-byte mechanism.

\begin{table}[htbp]
\centering
\caption{A descriptive comparison of bytes per emitted token and fixed-counter costs. Bytes per token includes the terminal \code{Eof} in the denominator, consistently with Table~1. The two per-token columns are derived by multiplying cycles per byte by bytes per token; counter speedup is flex cycles per byte divided by Monkey cycles per byte.}
\label{tab:cost-scaling}
\small
\begin{tabular}{lrrrrr}
\toprule
\rowcolor{TableHeader}
Corpus & \shortstack{Bytes/\\token} & \shortstack{Flex cycles/\\byte} & \shortstack{Monkey cycles/\\token} & \shortstack{Flex cycles/\\token} & \shortstack{Counter\\speedup}\\
\midrule
\code{real-src} & 4.1 & 19.4 & 47.6 & 79.9 & $1.68\times$\\
\code{real-libstdcxx} & 8.0 & 14.8 & 63.4 & 118.3 & $1.87\times$\\
\code{code-small} & 8.6 & 16.7 & 66.9 & 143.7 & $2.15\times$\\
\code{code-large} & 10.1 & 14.9 & 64.1 & 150.5 & $2.35\times$\\
\code{comment-large} & 12.1 & 14.8 & 73.5 & 179.3 & $2.44\times$\\
\bottomrule
\end{tabular}
\end{table}

Figure~\ref{fig:cost-scaling} plots the counter-derived speedup against bytes per token. The upward pattern is descriptive across five different corpora, not a causal estimate: token mix, whitespace, and corpus source still vary together.

\begin{figure}[htbp]
\centering
\includegraphics[width=0.78\linewidth]{cost_scaling_chart.png}
\caption{Counter-derived flex/Monkey cycles-per-byte ratio against bytes per emitted token. The five corpus profiles are not independent controlled levels, so the trend is exploratory.}
\label{fig:cost-scaling}
\end{figure}

To vary token and whitespace-run lengths independently, we generated fixed-size 4\,MiB inputs with identifier lengths 4, 16, or 64 bytes and target mean whitespace-run lengths 1, 8, or 32 bytes. The initial grid repeated \code{a...a}; a second version randomizes identifier characters with a fixed seed and, for target means 8 and 32, shuffles run lengths drawn from $1$--$15$ and $1$--$63$, respectively. A positive whitespace separator with mean one must be one byte, so that column has no run-length variation. Multiplying median cycles per byte by bytes per token gives the estimated cycle costs in Table~\ref{tab:control-grid}; branch misses per byte were collected separately in three additional randomized-order rounds. On randomized inputs, Flex's branch-miss rate is much higher than Monkey's in most cells, especially for short identifiers with longer or variable whitespace runs. At mean run length eight, Flex cost rose from $5.80$ to $12.79$ cycles/byte for 4-byte identifiers and from $5.00$ to $7.88$ for 16-byte identifiers when moving from repeated to randomized input; corresponding randomized Flex branch-miss rates are $0.0877$ and $0.0414$ misses/byte. This is consistent with repeated identical tokens flattering Flex's DFA branch prediction, but the missing branch-counter measurements on the original repeated-input grid prevent a direct attribution. The randomized speedups span $0.73\times$--$7.21\times$, below the repeated-input grid's $0.81\times$--$8.70\times$. The ordering still generally favors longer identifiers, but the shortest-token results are noisy and changed materially with content and spacing. Monkey's estimated cost ranges from about $60$ to $127$ cycles per token, while Flex ranges from about $44$ to $921$; neither scanner has a constant per-token cost across the grid. This remains exploratory: only one generated content seed was used, CPU scaling was enabled, and the SMT sibling was not verified idle.

\begin{table}[htbp]
\centering
\caption{Exploratory randomized-content grid. All inputs are 4\,MiB with fixed identifier lengths and target mean whitespace-run lengths. Cells report estimated cycles per token as Monkey/Flex, derived from the preceding three-round median cycles/byte and bytes/token, followed by median branch misses per byte as Monkey/Flex from three separately collected counter rounds. Identifier characters and run-length order use fixed seed 20261007; positive runs with target mean one are necessarily all one byte. Scanner/cell order was shuffled between rounds on core 2. CPU scaling was enabled, the SMT sibling was not verified idle, and no confidence intervals are reported.}
\label{tab:control-grid}
\small
\begin{tabular}{lrrr}
\toprule
\rowcolor{TableHeader}
Identifier length & Mean run 1 & Mean run 8 & Mean run 32\\
\midrule
4 bytes & \shortstack{$60/44$ cyc/token\\$0.0017/0.0026$ miss/byte} & \shortstack{$66/153$ cyc/token\\$0.0017/0.0877$ miss/byte} & \shortstack{$84/411$ cyc/token\\$0.0017/0.0509$ miss/byte}\\
16 bytes & \shortstack{$70/103$ cyc/token\\$0.0017/0.0017$ miss/byte} & \shortstack{$75/189$ cyc/token\\$0.0017/0.0414$ miss/byte} & \shortstack{$95/467$ cyc/token\\$0.0017/0.0380$ miss/byte}\\
64 bytes & \shortstack{$103/601$ cyc/token\\$0.0017/0.0172$ miss/byte} & \shortstack{$114/672$ cyc/token\\$0.0017/0.0299$ miss/byte} & \shortstack{$127/921$ cyc/token\\$0.0017/0.0299$ miss/byte}\\
\bottomrule
\end{tabular}
\end{table}

A previously explored regression of total cycles against bytes, tokens, whitespace runs, and scanner indicators cannot distinguish costs: it used only five distinct input profiles, the corpus predictors were highly correlated, and additive scanner indicators do not represent scanner-specific slopes. We therefore do not interpret its coefficients. The randomized-content grid reduces exact identifier repetition and samples multiple whitespace-run lengths, but each cell still uses one fixed content seed; replication across seeds and sessions and mixed token classes is needed before drawing a cost model. As a copy-error check, a fresh three-run measurement of the prior repeated-id 4+1 cell gave $12.26$ cycles/byte, compared with $11.57$ in its original single run; a fresh long fixed-iteration \code{real-src} run measured $11.73$ cycles/byte. The exact equality that prompted the check did not recur, though these measurements do not prove whether it was coincidence or session noise. Token frequencies do not locate branch misses; sampled profiles are still needed. The measured gap also does not separate flex runtime costs from its DFA; follow-up controls should disable \code{yylineno}, in-place NUL patching, per-token \code{yylex} returns, and action dispatch individually, then compare with a hand-written scalar table-driven DFA.

On \code{code-large}, branch misses per byte are $0.069$ for the shipped lexer, $0.118$ for \code{Scalar}, and $0.119$ for flex. The rough recovery-latency estimate does not isolate branch effects or identify their locations. Token density across the corpora is confounded with whitespace and comment share, and both \code{//} comments and \texttt{\#}-started lines are absent from the block-comment composition count.

\subsection{Limitations}
\label{sec:limitations}
All results come from a single core of an Intel Core i7-11370H. We did not validate top-down attribution with raw per-core events and an idle SMT sibling, sample and annotate branch misses, repeat the controlled corpus grid across sessions or with less repetitive inputs, measure a hybrid whitespace path, run a null-consumer scan or flex-feature/scalar-DFA ablations, or randomize and pair the whole ablation ladder. Session confidence intervals do not capture between-session drift. Every timed lexer also stores tokens, so the measurements include output traffic that a streaming consumer might avoid.

\needspace{8\baselineskip}
\section{Conclusion}
The two real-code corpora show a $1.85\times$ geometric-mean speedup over flex in the lex-only setup. The three synthetic corpora raise the five-corpus mean to $2.10\times$. These results apply to the tested hardware and scanner configurations. We do not headline the historical pipeline ratio because the harness allocates and first-touches a fresh token vector each iteration.

On the vendored libstdc++ input, SIMD cuts instructions per byte by about $19\,\%$ (15.24 to 12.37); on generated \code{code-large}, the reduction is $48\,\%$ (14.94 to 7.83). Neither reduction translates proportionally into elapsed time. The \code{Scalar} ablation is near parity with the shipped lexer. The best alternative \code{FlatIdents} estimates range between $1.07\times$ and $1.14\times$ the shipped lexer. The available measurements do not yet establish which per-token or speculative costs explain the remaining time.

Future work should repeat the randomized-content grid across seeds and sessions, run a null-consumer scan and a matched-permission reusable-buffer pipeline, add per-feature \code{flex} ablations and a scalar DFA, and measure a hybrid whitespace scanner. In summary, while SIMD reduced instructions by $19\,\%$ on real libstdc++ and $48\,\%$ on \code{code-large}, it did not comparably reduce time relative to the related bytewise ablation.

\begingroup
\setlength{\parskip}{0pt}
\raggedright
\begin{thebibliography}{12}
\bibitem{langdale2019}
Geoff Langdale and Daniel Lemire.
\newblock Parsing gigabytes of JSON per second.
\newblock \emph{The VLDB Journal}, 28(6):941--960, 2019.
\newblock arXiv:1902.08318.
\newblock \url{https://arxiv.org/abs/1902.08318}

\bibitem{mison}
Yinan Li, Nikos R. Katsipoulakis, Badrish Chandramouli, Jonathan Goldstein, and Donald Kossmann.
\newblock Mison: A fast JSON parser for data analytics.
\newblock \emph{Proceedings of the VLDB Endowment}, 10(10):1118--1129, 2017.
\newblock \url{https://doi.org/10.14778/3115404.3115416}

\bibitem{parabix}
Dan Lin, Nigel Medforth, Kenneth S. Herdy, Arrvindh Shriraman, and Rob Cameron.
\newblock Parabix: Boosting the efficiency of text processing on commodity processors.
\newblock In \emph{2012 IEEE 18th International Symposium on High Performance Computer Architecture}, pages 1--12, 2012.
\newblock \url{https://doi.org/10.1109/HPCA.2012.6169041}

\bibitem{clanglexer}
LLVM Project.
\newblock \code{clang/lib/Lex/Lexer.cpp}, \code{Lexer::SkipWhitespace} and \code{Lexer::SkipBlockComment}.
\newblock \url{https://github.com/llvm/llvm-project/blob/main/clang/lib/Lex/Lexer.cpp}

\bibitem{hyperscan}
Intel Corporation.
\newblock \emph{Introduction to Hyperscan}.
\newblock Technical article, 2017.
\newblock \url{https://www.intel.com/content/www/us/en/developer/articles/technical/introduction-to-hyperscan.html}
\newblock Implementation: \url{https://github.com/intel/hyperscan}

\bibitem{ragel}
Colm Networks.
\newblock \emph{Ragel State Machine Compiler}.
\newblock \url{https://www.colm.net/open-source/ragel/}

\bibitem{intel}
Intel Corporation.
\newblock \emph{Intel 64 and IA-32 Architectures Software Developer's Manual}.
\newblock Volume 2: Instruction Set Reference.
\newblock Entries for BZHI, VPCMPEQB/VPCMPEQW/VPCMPEQD/VPCMPEQQ, POPCNT, and TZCNT.
\newblock \url{https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html}

\bibitem{flex}
The flex Project.
\newblock \emph{The flex Manual}.
\newblock \url{https://westes.github.io/flex/manual/}

\bibitem{mmap}
Linux man-pages Project.
\newblock \emph{mmap(2): Map or Unmap Files or Devices into Memory}.
\newblock \url{https://man7.org/linux/man-pages/man2/mmap.2.html}

\bibitem{intelcpu}
Intel Corporation.
\newblock \emph{Intel Core i7-11370H Processor: Specifications}.
\newblock \url{https://www.intel.com/content/www/us/en/products/sku/196655/intel-core-i711370h-processor-12m-cache-up-to-4-80-ghz-with-ipu/specifications.html}.

\bibitem{madvise}
Linux man-pages Project.
\newblock \emph{madvise(2): Give Advice about Use of Memory}.
\newblock \url{https://man7.org/linux/man-pages/man2/madvise.2.html}

\bibitem{re2c}
The re2c Project.
\newblock \emph{re2c: a lexer generator for C, C++ and Go}.
\newblock \url{https://re2c.org}
\end{thebibliography}
\endgroup
\clearpage

\appendix
\section{Supplementary tables}
\renewcommand{\thetable}{A.\arabic{table}}
\setcounter{table}{0}

\begin{table}[htbp]
\centering
\caption{Identifier-side ablation split, throughput in decimal GB/s. The final row is the five-corpus geometric mean relative to \code{Scalar}.}
\label{tab:ablation-scan-kw}
\begin{tabular}{lrrrr}
\toprule
\rowcolor{TableHeader}
Corpus & \code{Scalar} & \code{KwOnly} & \code{IdentScan} & \code{Idents}\\
\midrule
\code{real-src}     & 0.3707 & 0.3857 & 0.4251 & 0.4531\\
\code{real-libstdcxx} & 0.5406 & 0.5459 & 0.6059 & 0.6333\\
\code{code-small}   & 0.5699 & 0.5058 & 0.5889 & 0.6226\\
\code{code-large}   & 0.6709 & 0.6325 & 0.6845 & 0.6897\\
\code{comment-large} & 0.7114 & 0.6751 & 0.6715 & 0.7735\\
\midrule
Relative to \code{Scalar} & $1.00\times$ & $0.96\times$ & $1.05\times$ & $1.12\times$\\
\bottomrule
\end{tabular}
\end{table}

\begin{table}[htbp]
\centering
\caption{Counters on the ablation ladder from fixed-iteration \code{perf} runs.}
\label{tab:ablation-counters}
\begin{tabular}{llrrr}
\toprule
\rowcolor{TableHeader}
Corpus & Lexer & Instructions / byte & Cycles / byte & IPC\\
\midrule
\code{code-large} & \code{Scalar} & 14.94 & 6.85 & 2.18\\
 & \code{Masks} & 9.27 & 7.11 & 1.30\\
 & \code{KwOnly} & 14.91 & 6.80 & 2.19\\
 & \code{IdentScan} & 13.53 & 6.57 & 2.06\\
 & \code{Idents} & 13.55 & 5.99 & 2.26\\
 & \code{Flat} & 9.18 & 5.86 & 1.57\\
 & \code{FlatIdents} & 13.48 & 6.35 & 2.12\\
 & \code{Monkey} & 7.83 & 6.36 & 1.23\\
 & \code{Flex} & 22.86 & 14.94 & 1.53\\
\midrule
\code{real-libstdcxx} & \code{Scalar} & 15.24 & 8.11 & 1.88\\
 & \code{Masks} & 15.26 & 9.26 & 1.65\\
 & \code{KwOnly} & 15.04 & 7.97 & 1.89\\
 & \code{IdentScan} & 12.47 & 7.54 & 1.65\\
 & \code{Idents} & 12.36 & 6.74 & 1.83\\
 & \code{Flat} & 11.97 & 7.37 & 1.63\\
 & \code{FlatIdents} & 11.93 & 6.93 & 1.72\\
 & \code{Monkey} & 12.37 & 7.95 & 1.56\\
 & \code{Flex} & 32.88 & 14.84 & 2.22\\
\bottomrule
\end{tabular}
\end{table}

\begin{table}[htbp]
\centering
\caption{Session, paired, and counter estimates of each cell's ratio to \code{Monkey}. The session confidence intervals use an independent-repetition approximation and do not include between-session drift. Paired wins are out of 60.}
\label{tab:repro}
\begin{tabular}{lccccc}
\toprule
\rowcolor{TableHeader}
Cell & Session GM & 95\% CI & Paired GM & Counter GM & Wins\\
\midrule
\code{Scalar}     & $0.986$ & $[0.964,\ 1.008]$ & $0.992$ & $0.95$ & $22/60$\\
\code{Masks}      & $0.869$ & $[0.854,\ 0.884]$ & ---     & $0.88$ & ---\\
\code{KwOnly}     & $0.951$ & $[0.933,\ 0.970]$ & ---     & $0.97$ & ---\\
\code{IdentScan}  & $1.036$ & $[1.013,\ 1.059]$ & ---     & $1.01$ & ---\\
\code{Idents}     & $1.103$ & $[1.081,\ 1.125]$ & $1.144$ & $1.12$ & $60/60$\\
\code{Flat}       & $1.079$ & $[1.057,\ 1.102]$ & ---     & $1.08$ & ---\\
\code{FlatIdents} & $1.122$ & $[1.100,\ 1.144]$ & $1.130$ & $1.07$ & $52/60$\\
\code{Copy}       & $0.984$ & $[0.965,\ 1.003]$ & $0.990$ & ---    & $20/60$\\
\code{Flex}       & $0.477$ & $[0.464,\ 0.490]$ & ---     & $0.48$ & ---\\
\bottomrule
\end{tabular}
\end{table}

\begin{table}[htbp]
\centering
\caption{Historical \emph{Pipeline} throughput and speedup in decimal GB/s, from the original harness that allocates a new token buffer on each iteration. These are end-to-end results only; they do not identify mapping cost or establish a fixed setup penalty.}
\label{tab:pipeline}
\begin{tabular}{lrrrrrr}
\toprule
\rowcolor{TableHeader}
& \multicolumn{2}{c}{\emph{Lex}} & \multicolumn{2}{c}{\emph{Pipeline}} & \multicolumn{2}{c}{Speedup}\\
\cmidrule(lr){2-3}\cmidrule(lr){4-5}\cmidrule(lr){6-7}
Corpus & \code{Monkey} & \code{Flex} & \code{Monkey} & \code{Flex} & \emph{Lex} & \emph{Pipeline}\\
\midrule
\code{real-src}     & 0.3682 & 0.2184 & 0.3683 & 0.2176 & $1.69\times$ & $1.69\times$\\
\code{real-libstdcxx} & 0.5455 & 0.2699 & 0.3638 & 0.2187 & $2.03\times$ & $1.66\times$\\
\code{code-small}   & 0.6210 & 0.2789 & 0.6314 & 0.2721 & $2.23\times$ & $2.32\times$\\
\code{code-large}   & 0.6281 & 0.2904 & 0.4055 & 0.2165 & $2.16\times$ & $1.87\times$\\
\code{comment-large} & 0.7462 & 0.3024 & 0.4565 & 0.2145 & $2.47\times$ & $2.13\times$\\
\midrule
\multicolumn{4}{l}{Geometric mean speedup} & & \multicolumn{2}{r}{$2.10\times$ \quad $1.92\times$}\\
\bottomrule
\end{tabular}
\end{table}

\begin{table}[htbp]
\centering
\caption{Separately timed read-only mapping and file-setup operations for \code{code-large}, using the \code{Monkey} padded reservation, \code{MAP_POPULATE} file overlay, and \code{madvise} flags. Each stage reports per-call median and p90 over 1,000 iterations after 50 warmups, pinned to core 2. Stage medians are not a median of their sum.}
\label{tab:map-syscalls}
\begin{tabular}{lrr}
\toprule
\rowcolor{TableHeader}
Stage & Median ($\mu$s) & p90 ($\mu$s)\\
\midrule
\code{open} & 2.2 & 4.9\\
\code{fstat} & 0.4 & 0.9\\
Anonymous reservation \code{mmap} & 1.2 & 2.7\\
Read-only file overlay with \code{MAP_POPULATE} & 55.0 & 72.9\\
\code{madvise} & 8.0 & 8.7\\
\code{munmap} & 17.0 & 20.6\\
\code{close} & 1.0 & 2.2\\
\midrule
Sum of stage medians & 84.8 & ---\\
\bottomrule
\end{tabular}
\end{table}

\begin{table}[htbp]
\centering
\caption{Estimated cycles per emitted token, calculated from the fixed-iteration cycles-per-byte counters in Table~\ref{tab:work-per-byte} and Table~\ref{tab:ablation-counters}, multiplied by bytes per token. Dashes mark variants without a fixed-iteration counter measurement for that corpus.}
\label{tab:cycle-per-token}
\begin{tabular}{lrrrr}
\toprule
\rowcolor{TableHeader}
Corpus & \code{Scalar} & \code{Idents} & \code{Monkey} & \code{Flex}\\
\midrule
\code{real-src} & --- & --- & 47.6 & 79.9\\
\code{real-libstdcxx} & 64.6 & 53.7 & 63.4 & 118.3\\
\code{code-small} & --- & --- & 66.9 & 143.7\\
\code{code-large} & 69.0 & 60.3 & 64.1 & 150.5\\
\code{comment-large} & --- & --- & 73.5 & 179.3\\
\bottomrule
\end{tabular}
\end{table}

\begin{table}[htbp]
\centering
\caption{Token-type mix from the shipped lexer, excluding the terminal \code{Eof} token. Counts and percentages use all non-EOF tokens as the denominator; error tokens are reported separately and are not included in the other classes.}
\label{tab:token-mix}
\small
\begin{tabular}{lrrrrr}
\toprule
\rowcolor{TableHeader}
Corpus & Identifier & Constant & Keyword & Operator/punctuation & Error token\\
\midrule
\code{real-src} & 9{,}779 (34.2\%) & 272 (0.9\%) & 1{,}037 (3.6\%) & 15{,}008 (52.4\%) & 2{,}539 (8.9\%)\\
\code{real-libstdcxx} & 252{,}905 (41.0\%) & 5{,}865 (1.0\%) & 18{,}723 (3.0\%) & 311{,}466 (50.5\%) & 27{,}810 (4.5\%)\\
\code{code-small} & 5{,}065 (16.4\%) & 4{,}665 (15.1\%) & 3{,}423 (11.1\%) & 17{,}736 (57.4\%) & 0 (0.0\%)\\
\code{code-large} & 68{,}328 (16.3\%) & 64{,}278 (15.4\%) & 45{,}870 (11.0\%) & 239{,}831 (57.3\%) & 0 (0.0\%)\\
\code{comment-large} & 57{,}043 (16.5\%) & 52{,}597 (15.2\%) & 37{,}879 (11.0\%) & 198{,}295 (57.3\%) & 0 (0.0\%)\\
\bottomrule
\end{tabular}
\end{table}

\begin{table}[htbp]
\centering
\caption{Read-only and writable private \code{MAP_POPULATE} overlays for the 4,212,693-byte \code{code-large} input (1,029 file pages). Medians are from 1,000 iterations after 50 warmups, pinned to core 2. The writable overlay incurred one minor fault per file page during \code{mmap}; writing one byte per page afterward incurred no additional faults. Writable unmapping is measured separately from read-only unmapping.}
\label{tab:map-writable}
\small
\begin{tabular}{lrrrrr}
\toprule
\rowcolor{TableHeader}
Mapping & \code{mmap} ($\mu$s) & Map faults & Write pass ($\mu$s) & Write faults & \code{munmap} ($\mu$s)\\
\midrule
Read-only private & 55.0 & 40 & --- & --- & 17.0\\
Writable private & 758.1 & 1{,}029 & 6.5 & 0 & 90.6\\
\bottomrule
\end{tabular}
\end{table}

\FloatBarrier
\section{Scanner reconstruction}
\label{app:design}
This appendix describes enough of the measured scanner to reproduce its control structure. The source for the artifact is linked at commit \href{https://github.com/nirajandata/Monkey/tree/fda7b59b2d7c4386f67ccc8af44521f8f51bbb3a}{\code{fda7b59}}.

\paragraph{Input and token interface.}
The lexer recognizes 61 \code{TokenType} values: \code{Identifier}, \code{Constant}, the 16 keywords \code{if}, \code{else}, \code{int}, \code{void}, \code{return}, \code{goto}, \code{do}, \code{for}, \code{while}, \code{break}, \code{continue}, \code{switch}, \code{case}, \code{default}, \code{static}, and \code{extern}; the 41 operator and punctuation types \code{LParen}, \code{RParen}, \code{LBrace}, \code{RBrace}, \code{Semicolon}, \code{Divide}, \code{Complement}, \code{Subtract}, \code{Decrement}, \code{Add}, \code{Multiply}, \code{Remainder}, \code{Not}, \code{And}, \code{Or}, \code{Assign}, \code{Equal}, \code{NotEqual}, \code{LessThan}, \code{GreaterThan}, \code{LessOrEqual}, \code{GreaterOrEqual}, \code{BitwiseAnd}, \code{BitwiseOr}, \code{BitwiseXor}, \code{ShiftLeft}, \code{ShiftRight}, \code{Increment}, \code{AddAssign}, \code{SubtractAssign}, \code{MultiplyAssign}, \code{DivideAssign}, \code{RemainderAssign}, \code{BitwiseAndAssign}, \code{BitwiseOrAssign}, \code{BitwiseXorAssign}, \code{ShiftLeftAssign}, \code{ShiftRightAssign}, \code{QuestionMark}, \code{Colon}, and \code{Comma}; \code{Error}; and terminal \code{Eof}. Identifiers are ASCII letters or underscore followed by ASCII letters, digits, or underscore. Constants are digit sequences; if followed by a letter or underscore, the complete digit-plus-identifier sequence becomes one error token. Other unsupported characters are emitted as one error token per byte. The scanner skips whitespace, \texttt{\#}-started lines (including preprocessor directives), \code{//} line comments, and non-nesting \code{/* ... */} comments. Operators use one-, two-, or three-character lookahead, preferring the longest recognized spelling.

Each \code{Token} contains a \code{uint32_t}-backed \code{TokenType}, a \code{std::string_view} into the mapped input, and a \code{uint32_t} line number. It occupies 32 bytes on the measured x86-64 ABI. The token count in Table~\ref{tab:composition} includes the terminal \code{Eof}; token text is not copied.

\paragraph{Scanning and class masks.}
The shipped scanner loads 64 bytes at a time from its padded mapping. ASCII classification produces 64-bit masks: bit $i$ corresponds to byte $i$. For a class mask $M$, \code{popcount(M)} counts class members and \code{ctz(M)} locates the first set bit. Logical EOF is always checked against the unpadded input limit.
\begin{verbatim}
skip_whitespace():
    while true:
        ws = whitespace_mask(load64(cursor))
        if ws == ALL:
            line += popcount(newline_mask(load64(cursor)))
            cursor += 64
            continue
        n = ctz(~ws)
        line += popcount(newline_mask(load64(cursor)) & low_bits(n))
        cursor += n
        return

while cursor < end:
    skip_whitespace()
    if cursor == end: break
    if identifier_start(*cursor): scan_identifier_and_advance()
    else if digit(*cursor):       scan_integer_and_advance()
    else if comment_start:        skip_comment_and_track_lines()
    else:                         emit_longest_operator_or_error_and_advance()
emit(Eof, empty_text, line)
\end{verbatim}

Identifier and integer scanning advance the cursor past the token but do not change the line: their accepted character classes contain no newline. Operator and punctuation emission likewise advances by the matched one-, two-, or three-byte spelling without changing the line; a newline is consumed earlier by the whitespace path. In line comments the scanner searches vectorially for a newline and stops before it; the next whitespace pass consumes it and increments the line count. For block comments it checks each 64-byte chunk for an asterisk. A chunk without one contributes all newline bits and is skipped; when an asterisk is present, a scalar delimiter check advances until \code{*/}, including delimiters crossing chunk boundaries, while counting newlines. If EOF arrives first, the cursor remains at the input limit and the scanner sets its error flag; it emits no separate unterminated-comment token, then the outer loop ends and emits \code{Eof}.

For identifiers, each vector comparison creates a mask for ASCII alphanumeric bytes plus underscore. An all-ones mask advances by 64; otherwise \code{ctz(~mask)} gives the identifier length in the chunk. Integer scanning uses the analogous digit mask. The keyword map contains $K=16$ entries, padded to eight 64-bit lanes per vector. For lengths from one through eight, up to eight input bytes are packed into a word and masked to the identifier length; that word is broadcast and compared against the keyword lanes, using two vector comparisons for the 16 entries. A zero hit mask means \code{Identifier}; otherwise the first hit selects the keyword type.

\paragraph{Ablation variants.}
\code{Scalar} replaces all 64-byte class loops and keyword lookup with bytewise loops and a sequential compare; it is an ablation, not a tuned scalar implementation. \code{Masks} restores vector whitespace/comment masks but leaves identifier scanning and keyword lookup scalar. \code{Idents} vectorizes identifier, integer, and keyword handling while whitespace and comments are scanned bytewise. \code{IdentScan} and \code{KwOnly} separate the vectorized extent scan from packed keyword lookup. \code{Flat} computes whitespace, newline, alphanumeric, digit, and asterisk masks once per 64-byte window and reuses them across scanner helpers; \code{FlatIdents} combines those cached identifier masks with scalar whitespace scanning. No hybrid whitespace fast path that branches on a scalar first byte was included.

\paragraph{Cycle model diagnostic.}
Table~\ref{tab:cycle-per-token} uses the fixed-iteration cycles-per-byte counters in Table~\ref{tab:work-per-byte} and Table~\ref{tab:ablation-counters}, multiplied by each corpus's bytes per emitted token. Counter cells were not collected for \code{Scalar} or \code{Idents} on three corpora and are left blank. The exploratory regression is not used because five corpus profiles and scanner-independent slopes cannot separate per-byte, per-token, and whitespace-run costs. The controlled $3\times3$ grid in Table~\ref{tab:control-grid} is a first diagnostic; it needs replication on more varied text.

\end{document}