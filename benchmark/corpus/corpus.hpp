#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace corpus {

struct Entry {
  std::string name;
  std::filesystem::path path;
  size_t bytes{0};
  std::string description;
};

struct Config {
  size_t small_bytes{256u * 1024u};
  size_t large_bytes{4u * 1024u * 1024u};
  size_t external_bytes{8u * 1024u * 1024u};
  uint64_t seed{0x5eed1234abcd0001ull};
};

namespace detail {

class Rng {
public:
  explicit Rng(uint64_t seed) : state_(seed ? seed : 0x9e3779b97f4a7c15ull) {}

  uint64_t next() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 0x2545f4914f6cdd1dull;
  }

  uint32_t below(uint32_t bound) {
    return static_cast<uint32_t>((next() >> 33) % bound);
  }

  bool chance(uint32_t percent) { return below(100) < percent; }

private:
  uint64_t state_;
};

inline std::string_view pick(std::initializer_list<std::string_view> choices,
                             Rng &rng) {
  const uint32_t index = rng.below(static_cast<uint32_t>(choices.size()));
  auto it = choices.begin();
  for (uint32_t i = 0; i < index && it != choices.end(); ++i)
    ++it;
  return it == choices.end() ? std::string_view{} : *it;
}

template <std::size_t N>
inline std::string_view pick(const char *const (&choices)[N], Rng &rng) {
  return choices[rng.below(static_cast<uint32_t>(N))];
}

class Generator {
public:
  Generator(Rng &rng, const Config &cfg) : rng_(rng), cfg_(cfg) {}

  void run(size_t target, bool comment_heavy) {
    out_.reserve(target + 4096);
    while (out_.size() < target)
      emit_function(comment_heavy);
    out_.push_back('\n');
  }

  const std::string &str() const { return out_; }

private:
  Rng &rng_;
  const Config &cfg_;
  std::string out_;
  size_t counter_{0};

  uint32_t spaces() const { return 1 + (rng_.below(3) * 4); }
  uint32_t comment_percent(bool heavy) const {
    return heavy ? 40 + rng_.below(11) : rng_.below(9);
  }

  void append_indent(uint32_t depth) {
    const uint32_t width = spaces() * depth;
    out_.append(width, ' ');
  }

  std::string name() {
    static const char *const roots[] = {"value", "counter",  "index",
                                       "buffer", "result",  "node",
                                       "slot",   "handle",  "context",
                                       "total",  "offset",  "length",
                                       "fn",     "state",   "entry",
                                       "a_very_long_identifier_that_exceeds_the_keyword_map_word",
                                       "tmp",    "param",   "acc"};
    std::string out = roots[rng_.below(sizeof(roots) / sizeof(roots[0]))];
    if (rng_.chance(45))
      out += std::to_string(rng_.below(1000));
    if (rng_.chance(8))
      out += "_" + std::to_string(counter_++);
    return out;
  }

  void emit_line_comment() {
    static const char *const texts[] = {
        " fast path for the common case",
        " TODO: the parser still rewrites this",
        " keep in sync with the semantic pass",
        " values below 4096 are treated as indices",
        " see also the codegen module"};
    const char *marker = rng_.chance(25) ? "#" : "//";
    if (rng_.chance(60)) {
      out_ += marker;
      out_ += pick(texts, rng_);
    } else {
      out_ += marker;
      out_ += " ";
      out_ += pick(texts, rng_);
    }
    out_ += '\n';
  }

  void emit_block_comment() {
    const uint32_t lines = 1 + rng_.below(4);
    out_ += "/*";
    for (uint32_t i = 0; i < lines; ++i) {
      out_ += "\n ";
      out_ += pick({"scanner entry point", "one token per call",
                    "lines are counted by the lexer", "no allocation here",
                    "see the reference scanner"},
                   rng_);
    }
    out_ += " */\n";
  }

  void emit_comment(bool heavy) {
    if (!rng_.chance(comment_percent(heavy)))
      return;
    if (rng_.chance(30))
      emit_block_comment();
    else
      emit_line_comment();
  }

  void emit_expression(uint32_t depth) {
    switch (rng_.below(depth < 2 ? 10u : 6u)) {
    case 0:
      out_ += name();
      break;
    case 1:
      out_ += std::to_string(rng_.below(100000));
      break;
    case 2:
      out_ += std::to_string(rng_.below(1000000)) + "000";
      break;
    case 3:
      emit_expression(depth + 1);
      out_ += ' ';
      out_ += pick({"+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^", "<",
                    ">", "<=", ">=", "==", "!=", "&&", "||"},
                   rng_);
      out_ += ' ';
      emit_expression(depth + 1);
      break;
    case 4:
      out_ += pick({"(", "-", "~", "!", "++", "--"}, rng_);
      emit_expression(depth + 1);
      out_ += ')';
      break;
    case 5:
      out_ += pick({"sizeof", "abs", "compute", "release"}, rng_);
      out_ += '(';
      emit_expression(depth + 1);
      out_ += ", ";
      emit_expression(depth + 1);
      out_ += ')';
      break;
    case 6:
      out_ += '*';
      out_ += name();
      break;
    case 7:
      out_ += '(';
      emit_expression(depth + 1);
      out_ += " ? ";
      emit_expression(depth + 1);
      out_ += " : ";
      emit_expression(depth + 1);
      out_ += ')';
      break;
    case 8:
      out_ += "goto ";
      out_ += name();
      break;
    default:
      out_ += std::to_string(rng_.below(1000000)) + " + 1";
      break;
    }
  }

  void emit_statement(uint32_t depth, bool heavy) {
    if (depth < 2 && rng_.chance(12))
      out_ += '\n';
    emit_comment(heavy);
    append_indent(depth);

    switch (rng_.below(depth < 2 ? 16u : 8u)) {
    case 0:
      out_ += pick({"int ", "void ", "static int "}, rng_);
      out_ += name();
      out_ += " = ";
      emit_expression(depth);
      out_ += ";\n";
      break;
    case 1:
      out_ += name();
      out_ += " = ";
      emit_expression(depth);
      out_ += ";\n";
      break;
    case 2:
      out_ += name();
      out_ += pick({" += ", " -= ", " *= ", " /= ", " %= ", " <<= ", " >>= ",
                    " &= ", " |= ", " ^= "},
                   rng_);
      emit_expression(depth);
      out_ += ";\n";
      break;
    case 3:
      out_ += "if (";
      emit_expression(depth);
      out_ += ") ";
      emit_statement(depth + 1, heavy);
      if (rng_.chance(40)) {
        emit_comment(heavy);
        append_indent(depth);
        out_ += "else ";
        emit_statement(depth + 1, heavy);
      }
      break;
    case 4:
      out_ += "while (";
      emit_expression(depth);
      out_ += ") ";
      emit_statement(depth + 1, heavy);
      break;
    case 5:
      out_ += "for (int ";
      out_ += name();
      out_ += " = 0; ";
      out_ += name();
      out_ += " < ";
      out_ += std::to_string(rng_.below(64));
      out_ += "; ";
      out_ += name();
      out_ += rng_.chance(50) ? "++" : " += 2";
      out_ += ") ";
      emit_statement(depth + 1, heavy);
      break;
    case 6:
      out_ += "do ";
      emit_statement(depth + 1, heavy);
      append_indent(depth);
      out_ += "while (";
      emit_expression(depth);
      out_ += ");\n";
      break;
    case 7:
      out_ += "switch (";
      emit_expression(depth);
      out_ += ") {\n";
      for (uint32_t i = 0; i < 2 + rng_.below(3); ++i) {
        emit_comment(heavy);
        append_indent(depth + 1);
        out_ += "case ";
        out_ += std::to_string(rng_.below(8));
        out_ += ": ";
        emit_statement(depth + 1, heavy);
      }
      emit_comment(heavy);
      append_indent(depth + 1);
      out_ += "default: break;\n";
      append_indent(depth);
      out_ += "}\n";
      break;
    case 8:
      out_ += pick({"return ", "return"}, rng_);
      emit_expression(depth);
      out_ += ";\n";
      break;
    case 9:
      out_ += name();
      out_ += '(';
      emit_expression(depth);
      out_ += ", ";
      emit_expression(depth);
      out_ += ");\n";
      break;
    case 10:
      out_ += '*';
      out_ += name();
      out_ += " = ";
      emit_expression(depth);
      out_ += ";\n";
      break;
    case 11:
      out_ += "if (";
      emit_expression(depth);
      out_ += ")\n";
      append_indent(depth + 1);
      out_ += "goto ";
      out_ += name();
      out_ += ";\n";
      return;
    case 12:
      out_ += "continue;\n";
      break;
    case 13:
      out_ += "break;\n";
      break;
    case 14:
      out_ += name();
      out_ += "++;\n";
      break;
    default:
      out_ += "{ ";
      emit_expression(depth);
      out_ += " }\n";
      break;
    }

    emit_comment(heavy);
  }

  void emit_function(bool heavy) {
    if (rng_.chance(70))
      emit_comment(heavy);

    if (rng_.chance(8))
      out_ += "static ";

    const std::string_view return_type = pick({"int", "void"}, rng_);
    out_ += return_type;
    out_ += ' ';
    const std::string fn = name();
    out_ += fn;
    out_ += '(';
    if (rng_.chance(30))
      out_ += "void";
    else
      for (uint32_t i = 0, n = rng_.below(4); i < n; ++i) {
        if (i)
          out_ += ", ";
        out_ += "int ";
        out_ += name();
      }
    out_ += ") {\n";

    for (uint32_t i = 0, n = 2 + rng_.below(10); i < n; ++i)
      emit_statement(1, heavy);

    emit_comment(heavy);
    out_ += "  return";
    if (return_type != "void") {
      out_ += ' ';
      out_ += std::to_string(rng_.below(1000));
    }
    out_ += ";\n}\n\n";
  }
};

inline std::string edge_cases() {
  std::string text = R"CORPUS(/* Monkey lexer edge cases.
 * Every construct here is lexically ambiguous, at a buffer boundary, or
 * produces a token that the hand written lexer and the flex scanner could
 * plausibly disagree on.
 */
# hash comment at the very start of a line
// slash comment directly after a hash comment
/* block */ /* two block comments on one line */
/* a block
   comment that
   spans three lines
   with a * inside and a * / pair */
if ifx iffy _if if_ i_f
int intx xint
void voidx
return returnx
goto gotox
do dox for forx while whilex break breakx continue continuex
switch switchx case casex default defaultx static staticx extern externx
externs goto_like keyword_with_a_very_long_name_that_is_over_eight_bytes
0123456789 0 1 42 123456789
1a 12ab 123_ 0x10 99z 7_7
a_very_long_identifier_that_is_definitely_more_than_sixty_four_bytes_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
_
__ ___ ____ _____
+ ++ += - -- -= * ** *= / // /* */ /= % %= ^ ^= ~ ! != & && &= | || |= = == < << <<= <= > >> >>= >= ? : , ; ( ) { } [ ] @ $ ` \ " '`
0x 1e 1E 1u 1U 1l 1L
i f i f i f
if if if if if
<<= >>= <<>> <<<
/* unterminated-looking sequences: **/, */, /***/, /* **/
/ * / * / *
+++
--
<=<
>>>
"
}
#line 1 "edge"
/* unterminated block comment: neither lexer emits tokens for the rest of
   the file, both only set had_error_ and then append Eof */
)CORPUS";

  static const std::string_view marker = "#line 1 \"edge\"\n";
  const size_t at = text.find(marker);
  static const std::string nuls{"int before;\0\0int after_nuls;\0int more;\n", 33};
  text.insert(at, nuls);
  return text;
}

inline std::string concatenate(std::vector<std::filesystem::path> files) {
  std::sort(files.begin(), files.end());
  std::string out;
  for (const auto &file : files) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
      continue;
    out.append(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    out += '\n';
  }
  return out;
}

inline std::string real_sources(const std::filesystem::path &src_dir) {
  std::vector<std::filesystem::path> files;
  for (const auto &entry :
       std::filesystem::directory_iterator(src_dir)) {
    if (!entry.is_regular_file())
      continue;
    const std::string ext = entry.path().extension().string();
    if (ext == ".cppm" || ext == ".cpp" || ext == ".h")
      files.push_back(entry.path());
  }
  return concatenate(std::move(files));
}

inline std::vector<std::filesystem::path> libstdcxx_header_dirs() {
  std::vector<std::filesystem::path> dirs;
  for (const auto &entry : std::filesystem::directory_iterator("/usr/include/c++")) {
    if (entry.is_directory() && entry.path().filename().string().find_first_not_of("0123456789") == std::string::npos)
      dirs.push_back(entry.path() / "bits");
  }
  std::sort(dirs.begin(), dirs.end());
  return dirs;
}

inline std::vector<std::filesystem::path> libstdcxx_headers(size_t target_bytes) {
  std::vector<std::filesystem::path> files;
  size_t total = 0;
  for (const auto &dir : libstdcxx_header_dirs()) {
    std::vector<std::filesystem::path> in_dir;
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
      if (!entry.is_regular_file())
        continue;
      const std::string ext = entry.path().extension().string();
      if (ext == ".h" || ext == ".hpp")
        in_dir.push_back(entry.path());
    }
    std::sort(in_dir.begin(), in_dir.end());
    for (const auto &file : in_dir) {
      files.push_back(file);
      total += static_cast<size_t>(std::filesystem::file_size(file));
      if (total >= target_bytes)
        return files;
    }
  }
  return files;
}

inline bool write_if_needed(const std::filesystem::path &path,
                            const std::string &content) {
  std::error_code ec;
  {
    std::ifstream existing(path, std::ios::binary);
    if (existing) {
      std::string current;
      current.reserve(content.size());
      current.assign(std::istreambuf_iterator<char>(existing),
                     std::istreambuf_iterator<char>());
      if (current == content)
        return false;
    }
  }
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  return true;
}

} // namespace detail

inline std::vector<Entry> generate(const std::filesystem::path &dir,
                                   const std::filesystem::path &src_dir,
                                   const Config &cfg = {}) {
  namespace d = detail;
  std::filesystem::create_directories(dir);

  std::vector<Entry> entries;
  auto add = [&](const std::string &name, const std::string &content,
                 const char *description) {
    const auto path = dir / (name + ".txt");
    d::write_if_needed(path, content);
    entries.push_back(
        Entry{name, path, content.size(), description});
  };

  add("edge", d::edge_cases(), "hand written edge cases, verify only");
  add("real-src", d::real_sources(src_dir),
      "this repository's own C++ sources");

  if (cfg.external_bytes > 0) {
    std::string external;
    const char *description = "libstdc++ headers, external real-world C++";
#ifdef MONKEY_PINNED_CORPUS_DIR
    const std::filesystem::path pinned =
        std::filesystem::path(MONKEY_PINNED_CORPUS_DIR) / "real-libstdcxx.txt";
    if (std::filesystem::is_regular_file(pinned)) {
      std::ifstream in(pinned, std::ios::binary);
      external.assign(std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>());
      description = "libstdc++ headers from GCC 16, vendored snapshot";
    }
#endif
    if (external.empty())
      external = d::concatenate(d::libstdcxx_headers(cfg.external_bytes));
    if (external.empty())
      throw std::runtime_error(
          "external corpus is empty: no libstdc++ headers found under "
          "/usr/include/c++, pass --external-bytes 0 to skip it");
    add("real-libstdcxx", external, description);
  }

  {
    d::Rng rng{cfg.seed ^ 0x1111};
    d::Generator gen{rng, cfg};
    gen.run(cfg.small_bytes, false);
    add("code-small", gen.str(), "code dense, few comments");
  }
  {
    d::Rng rng{cfg.seed ^ 0x2222};
    d::Generator gen{rng, cfg};
    gen.run(cfg.large_bytes, false);
    add("code-large", gen.str(), "code dense, few comments");
  }
  {
    d::Rng rng{cfg.seed ^ 0x3333};
    d::Generator gen{rng, cfg};
    gen.run(cfg.large_bytes, true);
    add("comment-large", gen.str(), "~45% comments and whitespace");
  }

  return entries;
}

} // namespace corpus

