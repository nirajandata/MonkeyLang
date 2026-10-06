#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "corpus/corpus.hpp"
#include "monkey_flex_lexer.h"
#include "monkey_token_types.h"

import token;
import lexer;
import lexer_scalar;
import lexer_masks;
import lexer_idents;
import lexer_flat;
import lexer_copy;
import lexer_identscan;
import lexer_kwonly;
import lexer_flatidents;

#define MONKEY_ASSERT_TOKEN(name, value)                                     \
  static_assert(static_cast<uint32_t>(TokenType::name) ==                   \
                    static_cast<uint32_t>(value),                            \
                "token code drift: " #name " != " #value);
MONKEY_TOKEN_LIST(MONKEY_ASSERT_TOKEN)
#undef MONKEY_ASSERT_TOKEN

static_assert(sizeof(monkey_flex_token) == sizeof(Token));
static_assert(alignof(monkey_flex_token) == alignof(Token));
static_assert(offsetof(monkey_flex_token, type) == offsetof(Token, type));
static_assert(offsetof(monkey_flex_token, text) == offsetof(Token, text));
static_assert(offsetof(monkey_flex_token, line) == offsetof(Token, line));
static_assert(sizeof(std::string_view) == sizeof(monkey_flex_token::text) +
                                              sizeof(monkey_flex_token::len));

namespace {

#define MONKEY_TOKEN_NAME(name, value) #name,
constexpr std::string_view kTokenNames[] = {MONKEY_TOKEN_LIST(MONKEY_TOKEN_NAME)};
#undef MONKEY_TOKEN_NAME

constexpr bool kTokenValuesAreDense = [] {
  uint32_t expected = 1;
  bool ok = true;
#define MONKEY_TOKEN_DENSE(name, value)                                      \
  ok = ok && ((value) == expected);                                          \
  ++expected;
  MONKEY_TOKEN_LIST(MONKEY_TOKEN_DENSE)
#undef MONKEY_TOKEN_DENSE
  return ok;
}();
static_assert(kTokenValuesAreDense, "token codes must be 1..N in list order");

std::string_view type_name(TokenType type) {
  const uint32_t value = static_cast<uint32_t>(type);
  if (value == 0 || value >= MONKEY_TT_COUNT)
    return "<none>";
  return kTokenNames[value - 1];
}

void compare_against(std::string_view label,
                     const std::vector<Token> &got_tokens,
                     const std::vector<Token> &expected, std::string &mismatches) {
  const size_t common = std::min(expected.size(), got_tokens.size());
  for (size_t i = 0; i < common; ++i) {
    if (static_cast<uint32_t>(expected[i].type) !=
            static_cast<uint32_t>(got_tokens[i].type) ||
        expected[i].text != got_tokens[i].text ||
        expected[i].line != got_tokens[i].line) {
      mismatches += "  " + std::string(label) + " token " +
                    std::to_string(i) + " differs from Monkey: " +
                    std::string(type_name(got_tokens[i].type)) + " '" +
                    std::string(got_tokens[i].text) + "' line " +
                    std::to_string(got_tokens[i].line) + " != " +
                    std::string(type_name(expected[i].type)) + " '" +
                    std::string(expected[i].text) + "' line " +
                    std::to_string(expected[i].line) + '\n';
      return;
    }
  }
  if (expected.size() != got_tokens.size())
    mismatches += "  " + std::string(label) + " token count: " +
                  std::to_string(got_tokens.size()) + " != " +
                  std::to_string(expected.size()) + '\n';
}

std::vector<corpus::Entry> g_bench_corpora;
std::vector<corpus::Entry> g_verify_corpora;

constexpr size_t token_capacity_for(size_t file_bytes) {
  return file_bytes / 3 + 16;
}

struct VerifyReport {
  bool ok{false};
  size_t tokens{0};
  std::string message;
};

struct FlexVariant {
  const char *label;
  const monkey_flex_api *api;
};

constexpr FlexVariant kFlexVariants[] = {
    {"flex", &monkey_flex_api_base},
    {"flex-avxkw", &monkey_flex_api_avxkw},
    {"flex-cf", &monkey_flex_api_cf},
    {"re2c", &monkey_flex_api_re2c},
};

void compare_flex_against(std::string_view label,
                          const monkey_flex_token *got, size_t got_count,
                          const std::vector<Token> &expected, bool monkey_ok,
                          bool flex_ok, std::string &mismatches) {
  const size_t common = std::min(expected.size(), got_count);
  size_t reported = 0;

  for (size_t i = 0; i < common; ++i) {
    const Token &want = expected[i];
    const monkey_flex_token &tok = got[i];
    const std::string_view want_text = want.text;
    const std::string_view got_text(tok.text, tok.len);

    if (static_cast<uint32_t>(want.type) != static_cast<uint32_t>(tok.type) ||
        want_text != got_text || want.line != tok.line) {
      if (reported++ < 8) {
        std::string message = "  " + std::string(label) + " token " +
                              std::to_string(i) + ": monkey " +
                              std::string(type_name(want.type)) + " '";
        message.append(want_text);
        message += "' line ";
        message += std::to_string(want.line);
        message += " != " + std::string(label) + " " +
                   std::string(type_name(static_cast<TokenType>(tok.type))) +
                   " '";
        message.append(got_text);
        message += "' line ";
        message += std::to_string(tok.line);
        message += '\n';
        mismatches += message;
      }
    }
  }

  if (expected.size() != got_count) {
    if (reported++ < 8) {
      mismatches += "  " + std::string(label) + " token count: monkey " +
                    std::to_string(expected.size()) + " != " +
                    std::to_string(got_count) + '\n';
    }
  }

  if (monkey_ok != flex_ok) {
    mismatches += "  " + std::string(label) + " error flag: monkey " +
                  std::string(monkey_ok ? "ok" : "had errors") + " != " +
                  std::string(flex_ok ? "ok" : "had errors") + '\n';
  }
}

VerifyReport verify_corpus(const corpus::Entry &entry, bool verbose) {
  Lexer lexer(entry.path);
  lexer.lex();
  const std::vector<Token> &expected = lexer.get_tokens();
  const bool monkey_ok = lexer.ok();

  std::string ablated_mismatches;
  {
    monkey::ablate::scalar::Lexer scalar(entry.path);
    scalar.lex();
    compare_against("scalar", scalar.get_tokens(), expected, ablated_mismatches);
  }
  {
    monkey::ablate::masks::Lexer masks(entry.path);
    masks.lex();
    compare_against("masks", masks.get_tokens(), expected, ablated_mismatches);
  }
  {
    monkey::ablate::idents::Lexer idents(entry.path);
    idents.lex();
    compare_against("idents", idents.get_tokens(), expected, ablated_mismatches);
  }
  {
    monkey::ablate::identscan::Lexer identscan(entry.path);
    identscan.lex();
    compare_against("identscan", identscan.get_tokens(), expected,
                    ablated_mismatches);
  }
  {
    monkey::ablate::kwonly::Lexer kwonly(entry.path);
    kwonly.lex();
    compare_against("kwonly", kwonly.get_tokens(), expected, ablated_mismatches);
  }
  {
    monkey::ablate::flatidents::Lexer flatidents(entry.path);
    flatidents.lex();
    compare_against("flatidents", flatidents.get_tokens(), expected,
                    ablated_mismatches);
  }
  {
    monkey::ablate::flat::Lexer flat(entry.path);
    flat.lex();
    compare_against("flat", flat.get_tokens(), expected, ablated_mismatches);
  }

  const std::string path = entry.path.string();
  const size_t capacity = token_capacity_for(entry.bytes);

  std::string mismatches;
  if (!ablated_mismatches.empty()) {
    mismatches += "  ablated lexers do not match the shipped lexer:\n";
    mismatches += ablated_mismatches;
  }

  for (const FlexVariant &variant : kFlexVariants) {
    monkey_flex_lexer *flex = monkey_flex_open(path.c_str());
    if (flex == nullptr) {
      mismatches += "  " + std::string(variant.label) + " cannot open " +
                    path + ": " + monkey_flex_open_error() + '\n';
      continue;
    }

    auto buffer =
        std::make_unique_for_overwrite<monkey_flex_token[]>(capacity);
    size_t count = 0;
    int had_error = 0;
    const int rc =
        monkey_flex_lex(variant.api, flex, buffer.get(), capacity, &count,
                        &had_error);

    if (rc != 0) {
      mismatches += "  " + std::string(variant.label) + " lex failed (rc=" +
                    std::to_string(rc) + "): " + monkey_flex_open_error() +
                    '\n';
    } else {
      compare_flex_against(variant.label, buffer.get(), count, expected,
                           monkey_ok, !had_error, mismatches);
    }

    monkey_flex_close(variant.api, flex);
  }

  VerifyReport report;
  report.tokens = expected.size();
  report.ok = mismatches.empty();
  if (!report.ok)
    report.message = mismatches;

  if (verbose && report.ok) {
    std::println(
        "  {:<14} {:>7} tokens, {:>8} bytes, {} lines, errors {}: token streams "
        "match",
        entry.name, report.tokens, entry.bytes,
        expected.empty() ? 0u : expected.back().line,
        monkey_ok ? "no" : "yes");
  }

  return report;
}

bool verify_all(bool verbose) {
  bool ok = true;
  for (const corpus::Entry &entry : g_verify_corpora) {
    if (verbose)
      std::println("verifying {}", entry.name);
    const VerifyReport report = verify_corpus(entry, verbose);
    if (!report.ok) {
      ok = false;
      std::println("MISMATCH in {} ({} tokens):\n{}", entry.name, report.tokens,
                   report.message);
    }
  }
  return ok;
}

void count_output(benchmark::State &state, const corpus::Entry &entry,
                  size_t tokens) {
  const auto bytes = static_cast<int64_t>(entry.bytes);
  const int64_t iterations = state.iterations();
  state.SetBytesProcessed(iterations * bytes);
  state.SetItemsProcessed(iterations * static_cast<int64_t>(tokens));
  state.counters["corpus_bytes"] = static_cast<double>(entry.bytes);
  state.counters["tokens_per_run"] = benchmark::Counter(
      static_cast<double>(tokens) / static_cast<double>(iterations),
      benchmark::Counter::kIsIterationInvariant);
}

void bench_monkey_lex(benchmark::State &state, const corpus::Entry &entry) {
  Lexer lexer(entry.path);
  lexer.lex();
  const size_t tokens = lexer.get_tokens().size();

  for (auto _ : state) {
    lexer.lex();
    benchmark::ClobberMemory();
  }

  count_output(state, entry, tokens);
}

template <typename AblatedLexer>
void bench_ablated_lex(benchmark::State &state, const corpus::Entry &entry) {
  AblatedLexer lexer(entry.path);
  lexer.lex();
  const size_t tokens = lexer.get_tokens().size();

  for (auto _ : state) {
    lexer.lex();
    benchmark::ClobberMemory();
  }

  count_output(state, entry, tokens);
}

void bench_monkey_pipeline(benchmark::State &state, const corpus::Entry &entry) {
  size_t tokens = 0;

  for (auto _ : state) {
    Lexer lexer(entry.path);
    lexer.lex();
    tokens = lexer.get_tokens().size();
    benchmark::DoNotOptimize(lexer.get_tokens().data());
  }

  count_output(state, entry, tokens);
}

template <typename AblatedLexer>
void bench_ablated_pipeline(benchmark::State &state, const corpus::Entry &entry) {
  size_t tokens = 0;

  for (auto _ : state) {
    AblatedLexer lexer(entry.path);
    lexer.lex();
    tokens = lexer.get_tokens().size();
    benchmark::DoNotOptimize(lexer.get_tokens().data());
  }

  count_output(state, entry, tokens);
}

void bench_flex_variant_pipeline(benchmark::State &state,
                                 const corpus::Entry &entry,
                                 const monkey_flex_api *api);

void bench_flex_lex(benchmark::State &state, const corpus::Entry &entry) {
  const std::string path = entry.path.string();
  monkey_flex_lexer *flex = monkey_flex_open(path.c_str());
  if (flex == nullptr) {
    state.SkipWithError(monkey_flex_open_error());
    return;
  }

  const size_t capacity = token_capacity_for(entry.bytes);
  auto buffer = std::make_unique_for_overwrite<monkey_flex_token[]>(capacity);
  size_t tokens = 0;
  int had_error = 0;

  for (auto _ : state) {
    if (monkey_flex_lex(&monkey_flex_api_base, flex, buffer.get(), capacity,
                        &tokens, &had_error) != 0) {
      state.SkipWithError("token buffer overflow or scan failure");
      break;
    }
    benchmark::ClobberMemory();
  }

  monkey_flex_close(&monkey_flex_api_base, flex);
  count_output(state, entry, tokens);
}

void bench_flex_variant_lex(benchmark::State &state, const corpus::Entry &entry,
                            const monkey_flex_api *api) {
  const std::string path = entry.path.string();
  monkey_flex_lexer *flex = monkey_flex_open(path.c_str());
  if (flex == nullptr) {
    state.SkipWithError(monkey_flex_open_error());
    return;
  }

  const size_t capacity = token_capacity_for(entry.bytes);
  auto buffer = std::make_unique_for_overwrite<monkey_flex_token[]>(capacity);
  size_t tokens = 0;
  int had_error = 0;

  for (auto _ : state) {
    if (monkey_flex_lex(api, flex, buffer.get(), capacity, &tokens,
                        &had_error) != 0) {
      state.SkipWithError("token buffer overflow or scan failure");
      break;
    }
    benchmark::ClobberMemory();
  }

  monkey_flex_close(api, flex);
  count_output(state, entry, tokens);
}

void bench_flex_pipeline(benchmark::State &state, const corpus::Entry &entry) {
  bench_flex_variant_pipeline(state, entry, &monkey_flex_api_base);
}

void bench_flex_variant_pipeline(benchmark::State &state,
                                 const corpus::Entry &entry,
                                 const monkey_flex_api *api) {
  const std::string path = entry.path.string();
  size_t tokens = 0;

  for (auto _ : state) {
    monkey_flex_lexer *flex = monkey_flex_open(path.c_str());
    if (flex == nullptr) {
      state.SkipWithError(monkey_flex_open_error());
      return;
    }

    const size_t capacity = token_capacity_for(entry.bytes);
    auto buffer = std::make_unique_for_overwrite<monkey_flex_token[]>(capacity);
    int had_error = 0;
    if (monkey_flex_lex(api, flex, buffer.get(), capacity, &tokens,
                        &had_error) != 0) {
      monkey_flex_close(api, flex);
      state.SkipWithError("token buffer overflow or scan failure");
      return;
    }
    benchmark::DoNotOptimize(buffer.get());

    monkey_flex_close(api, flex);
  }

  count_output(state, entry, tokens);
}

void register_benchmarks() {
  for (const corpus::Entry &entry : g_bench_corpora) {
    const std::string base = entry.name;

    benchmark::RegisterBenchmark(
        ("Monkey/Lex/" + base).c_str(),
        [entry](benchmark::State &state) { bench_monkey_lex(state, entry); });
    benchmark::RegisterBenchmark(
        ("Flex/Lex/" + base).c_str(),
        [entry](benchmark::State &state) { bench_flex_lex(state, entry); });
    benchmark::RegisterBenchmark(
        ("FlexAVXKW/Lex/" + base).c_str(), [entry](benchmark::State &state) {
          bench_flex_variant_lex(state, entry, &monkey_flex_api_avxkw);
        });
    benchmark::RegisterBenchmark(
        ("FlexCF/Lex/" + base).c_str(), [entry](benchmark::State &state) {
          bench_flex_variant_lex(state, entry, &monkey_flex_api_cf);
        });
    benchmark::RegisterBenchmark(
        ("Re2c/Lex/" + base).c_str(), [entry](benchmark::State &state) {
          bench_flex_variant_lex(state, entry, &monkey_flex_api_re2c);
        });
    benchmark::RegisterBenchmark(
        ("Flat/Lex/" + base).c_str(), [entry](benchmark::State &state) {
          bench_ablated_lex<monkey::ablate::flat::Lexer>(state, entry);
        });
    benchmark::RegisterBenchmark(
        ("Monkey/Pipeline/" + base).c_str(),
        [entry](benchmark::State &state) { bench_monkey_pipeline(state, entry); });
    benchmark::RegisterBenchmark(
        ("Flex/Pipeline/" + base).c_str(),
        [entry](benchmark::State &state) { bench_flex_pipeline(state, entry); });
    benchmark::RegisterBenchmark(
        ("FlexAVXKW/Pipeline/" + base).c_str(), [entry](benchmark::State &state) {
          bench_flex_variant_pipeline(state, entry, &monkey_flex_api_avxkw);
        });
    benchmark::RegisterBenchmark(
        ("FlexCF/Pipeline/" + base).c_str(), [entry](benchmark::State &state) {
          bench_flex_variant_pipeline(state, entry, &monkey_flex_api_cf);
        });
    benchmark::RegisterBenchmark(
        ("Re2c/Pipeline/" + base).c_str(), [entry](benchmark::State &state) {
          bench_flex_variant_pipeline(state, entry, &monkey_flex_api_re2c);
        });
    benchmark::RegisterBenchmark(
        ("Scalar/Lex/" + base).c_str(),
        [entry](benchmark::State &state) {
          bench_ablated_lex<monkey::ablate::scalar::Lexer>(state, entry);
        });
    benchmark::RegisterBenchmark(
        ("Masks/Lex/" + base).c_str(),
        [entry](benchmark::State &state) {
          bench_ablated_lex<monkey::ablate::masks::Lexer>(state, entry);
        });
    benchmark::RegisterBenchmark(
        ("Idents/Lex/" + base).c_str(),
        [entry](benchmark::State &state) {
          bench_ablated_lex<monkey::ablate::idents::Lexer>(state, entry);
        });
    benchmark::RegisterBenchmark(
        ("IdentScan/Lex/" + base).c_str(),
        [entry](benchmark::State &state) {
          bench_ablated_lex<monkey::ablate::identscan::Lexer>(state, entry);
        });
    benchmark::RegisterBenchmark(
        ("KwOnly/Lex/" + base).c_str(),
        [entry](benchmark::State &state) {
          bench_ablated_lex<monkey::ablate::kwonly::Lexer>(state, entry);
        });
    benchmark::RegisterBenchmark(
        ("FlatIdents/Lex/" + base).c_str(),
        [entry](benchmark::State &state) {
          bench_ablated_lex<monkey::ablate::flatidents::Lexer>(state, entry);
        });
    benchmark::RegisterBenchmark(
        ("Copy/Lex/" + base).c_str(),
        [entry](benchmark::State &state) {
          bench_ablated_lex<monkey::ablate::copy::Lexer>(state, entry);
        });
    benchmark::RegisterBenchmark(
        ("Copy/Pipeline/" + base).c_str(),
        [entry](benchmark::State &state) {
          bench_ablated_pipeline<monkey::ablate::copy::Lexer>(state, entry);
        });
  }
}

struct Options {
  std::filesystem::path corpus_dir{MONKEY_BENCH_CORPUS_DIR};
  std::filesystem::path src_dir{MONKEY_BENCH_SRC_DIR};
  corpus::Config config;
  bool generate_only{false};
  bool verify_only{false};
  bool skip_verify{false};
  bool list_only{false};
  bool quiet{false};
};

void usage() {
  std::println(
      "usage: {} [--corpus-dir DIR] [--small-bytes N] [--large-bytes N]\n"
      "          [--external-bytes N] [--generate-only] [--verify-only]\n"
      "          [--no-verify] [--list] [--quiet] [google benchmark flags...]",
      "lexer_bench");
}

}

int main(int argc, char **argv) {
  Options options;

  std::vector<char *> forwarded;
  forwarded.push_back(argv[0]);

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const auto value_of = [&](const char *name) -> std::string {
      const std::string prefix = std::string(name) + "=";
      if (arg.starts_with(prefix))
        return std::string(arg.substr(prefix.size()));
      if (arg == name && i + 1 < argc)
        return argv[++i];
      return {};
    };

    if (const std::string value = value_of("--corpus-dir"); !value.empty()) {
      options.corpus_dir = value;
    } else if (const std::string value = value_of("--small-bytes");
               !value.empty()) {
      options.config.small_bytes = std::strtoull(value.c_str(), nullptr, 10);
    } else if (const std::string value = value_of("--large-bytes");
               !value.empty()) {
      options.config.large_bytes = std::strtoull(value.c_str(), nullptr, 10);
    } else if (const std::string value = value_of("--external-bytes");
               !value.empty()) {
      options.config.external_bytes = std::strtoull(value.c_str(), nullptr, 10);
    } else if (arg == "--generate-only") {
      options.generate_only = true;
    } else if (arg == "--verify-only") {
      options.verify_only = true;
    } else if (arg == "--no-verify") {
      options.skip_verify = true;
    } else if (arg == "--list") {
      options.list_only = true;
    } else if (arg == "--quiet") {
      options.quiet = true;
    } else if (arg == "--help" || arg == "-h") {
      usage();
      return 0;
    } else {
      forwarded.push_back(argv[i]);
    }
  }

  int forwarded_argc = static_cast<int>(forwarded.size());
  benchmark::Initialize(&forwarded_argc, forwarded.data());

  g_verify_corpora = corpus::generate(options.corpus_dir, options.src_dir,
                                     options.config);
  for (const corpus::Entry &entry : g_verify_corpora)
    if (entry.name != "edge")
      g_bench_corpora.push_back(entry);

  if (options.list_only) {
    for (const corpus::Entry &entry : g_verify_corpora)
      std::println("{:<14} {:>9} bytes  {}", entry.name, entry.bytes,
                   entry.description);
    return 0;
  }

  if (!options.quiet) {
    std::println("corpus directory: {}", options.corpus_dir.string());
    for (const corpus::Entry &entry : g_verify_corpora)
      std::println("  {:<14} {:>9} bytes  {}", entry.name, entry.bytes,
                   entry.description);
  }

  if (options.generate_only)
    return 0;

  if (!options.verify_only && !options.skip_verify) {
    if (!verify_all(!options.quiet)) {
      std::println("\ntoken streams differ: the comparison below would be "
                   "meaningless, fix the scanner first (or pass --no-verify to "
                   "time them anyway)");
      return 1;
    }
    if (!options.quiet)
      std::println("token streams match\n");
  }

  if (options.verify_only)
    return verify_all(!options.quiet) ? 0 : 1;

  register_benchmarks();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
