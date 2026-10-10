#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <meta>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

import token;
import ast;
import lexer;
import parser;
import semantic;
import nir;
import codegen;

static bool expand_local_includes(const std::filesystem::path &path,
                                  std::string &output,
                                  std::unordered_set<std::string> &included) {
  std::ifstream input(path);
  if (!input) return false;
  std::string line;
  while (std::getline(input, line)) {
    const size_t directive = line.find("#include");
    const size_t quote = directive == std::string::npos
                             ? std::string::npos
                             : line.find('"', directive + 8);
    if (quote != std::string::npos) {
      const size_t end = line.find('"', quote + 1);
      if (end != std::string::npos) {
        const auto included_path =
            std::filesystem::weakly_canonical(path.parent_path() /
                line.substr(quote + 1, end - quote - 1));
        const std::string key = included_path.string();
        if (included.insert(key).second &&
            !expand_local_includes(included_path, output, included))
          return false;
        output.push_back('\n');
        continue;
      }
    }
    output += line;
    output.push_back('\n');
  }
  return true;
}

enum class Stage : std::uint8_t {
  Lex,
  Parse,
  Validate,
  Nir,
  CodeGen,
  EmitAsm,
  Object,
  Run
};

static std::optional<Stage> parse_stage(std::string_view arg) {
  if (arg == "--lex")
    return Stage::Lex;
  if (arg == "--parse")
    return Stage::Parse;
  if (arg == "--validate")
    return Stage::Validate;
  if (arg == "--tacky" || arg == "--nir")
    return Stage::Nir;
  if (arg == "--codegen")
    return Stage::CodeGen;
  if (arg == "-S")
    return Stage::EmitAsm;
  if (arg == "-c")
    return Stage::Object;
  return std::nullopt;
}

static void print_tokens(const std::vector<Token> &tokens) {
  for (const auto &t : tokens) {
    if (t.type == TokenType::Eof) {
      std::println("EOF at line {}", t.line);
      continue;
    }
    std::println("Token: {:<2} | Line: {:<4} | Text: '{}'",
                 static_cast<uint32_t>(t.type), t.line, t.text);
  }
}

template <typename T>
consteval std::size_t count_ast_members() {
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()).size();
}

template <typename T>
consteval auto get_ast_members() {
    constexpr std::size_t N = count_ast_members<T>();
    std::array<std::meta::info, N> arr{};
    auto vec = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current());
    for (std::size_t i = 0; i < N; ++i) {
        arr[i] = vec[i];
    }
    return arr;
}

template <typename T>
consteval std::size_t count_ast_enumerators() {
    return std::meta::enumerators_of(^^T).size();
}

template <typename T>
consteval auto get_ast_enumerators() {
    constexpr std::size_t N = count_ast_enumerators<T>();
    std::array<std::meta::info, N> arr{};
    auto vec = std::meta::enumerators_of(^^T);
    for (std::size_t i = 0; i < N; ++i) {
        arr[i] = vec[i];
    }
    return arr;
}

static std::string type_name(const Type &type) {
  if (type.kind == TypeKind::Pointer)
    return "Pointer(" + (type.referenced ? type_name(*type.referenced) : "<?>") + ")";
  if (type.kind == TypeKind::Array)
    return "Array(" + (type.referenced ? type_name(*type.referenced) : "<?>") + ", " + std::to_string(type.size) + ")";

  std::string name = "";
  template for (constexpr auto e : get_ast_enumerators<decltype(type.kind)>()) {
    if (type.kind == [:e:]) {
      name = std::meta::identifier_of(e);
    }
  }

  if (name.empty() || name == "Function" || name == "FunType" || name == "Fun") {
    std::string fn_name = "FunType(";
    for (size_t i = 0; i < type.params.size(); ++i) {
      if (i) fn_name += ", ";
      fn_name += type_name(type.params[i]);
    }
    fn_name += " -> ";
    fn_name += type.ret ? type_name(*type.ret) : "Int";
    fn_name += ")";
    return fn_name;
  }

  return name;
}

template <typename T> constexpr bool is_variant_v = false;
template <typename... Ts> constexpr bool is_variant_v<std::variant<Ts...>> = true;

template <typename T> constexpr bool is_vector_v = false;
template <typename T, typename A> constexpr bool is_vector_v<std::vector<T, A>> = true;

template <typename T> constexpr bool is_optional_v = false;
template <typename T> constexpr bool is_optional_v<std::optional<T>> = true;

template <typename T> constexpr bool is_unique_ptr_v = false;
template <typename T, typename D> constexpr bool is_unique_ptr_v<std::unique_ptr<T, D>> = true;

template <typename T> constexpr bool is_shared_ptr_v = false;
template <typename T> constexpr bool is_shared_ptr_v<std::shared_ptr<T>> = true;

template <typename T> constexpr bool is_string_like_v =
    std::is_same_v<std::decay_t<T>, std::string> || std::is_same_v<std::decay_t<T>, std::string_view>;

template <typename T>
void pretty_print(const T& obj, int indent = 0) {
    std::string pad(indent * 2, ' ');

    if constexpr (std::is_same_v<std::decay_t<T>, Type>) {
        std::println("{}{}", pad, type_name(obj));
    }
    else if constexpr (is_string_like_v<T>) {
        std::println("{}\"{}\"", pad, obj);
    }
    else if constexpr (std::is_arithmetic_v<T>) {
        std::println("{}{}", pad, obj);
    }
    else if constexpr (std::is_enum_v<T>) {
        std::string_view name = "<unknown>";
        template for (constexpr auto e : get_ast_enumerators<T>()) {
            if (obj == [:e:]) {
                name = std::meta::identifier_of(e);
            }
        }
        std::println("{}{}", pad, name);
    }
    else if constexpr (is_optional_v<T> || is_unique_ptr_v<T> || is_shared_ptr_v<T> || std::is_pointer_v<T>) {
        if (obj) {
            pretty_print(*obj, indent);
        } else {
            std::println("{}<null>", pad);
        }
    }
    else if constexpr (is_variant_v<T>) {
        std::visit([indent](const auto& v) { pretty_print(v, indent); }, obj);
    }
    else if constexpr (is_vector_v<T>) {
        if (obj.empty()) {
            std::println("{}[]", pad);
        } else {
            std::println("{}[", pad);
            for (const auto& item : obj) {
                pretty_print(item, indent + 1);
            }
            std::println("{}]", pad);
        }
    }
    else if constexpr (std::is_class_v<T>) {
        if constexpr (count_ast_members<T>() == 0) {
            std::println("{}{}", pad, std::meta::identifier_of(^^T));
        }
        else if constexpr (count_ast_members<T>() == 1 &&
                      std::meta::identifier_of(get_ast_members<T>()[0]) == "value") {
            pretty_print(obj.[:get_ast_members<T>()[0]:], indent);
        }
        else {
            std::println("{}{}(", pad, std::meta::identifier_of(^^T));
            template for (constexpr auto mem : get_ast_members<T>()) {
                std::println("{}  {} =", pad, std::meta::identifier_of(mem));
                pretty_print(obj.[:mem:], indent + 2);
            }
            std::println("{})", pad);
        }
    }
    else {
        std::println("{}<unknown>", pad);
    }
}

static int compile_file(const std::filesystem::path &source_path, Stage stage,
                        std::string &assembly) {
  std::unordered_set<std::string> included;
  std::string preprocessed;
  if (!expand_local_includes(source_path, preprocessed, included)) return 1;
  const auto temporary_path = std::filesystem::temp_directory_path() /
      ("monkey-" + std::to_string(next_name_id()) + ".c");
  {
    std::ofstream temporary(temporary_path);
    if (!temporary) return 1;
    temporary << preprocessed;
  }
  struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() { std::error_code ec; std::filesystem::remove(path, ec); }
  } cleanup{temporary_path};
  Lexer lexer(temporary_path);
  lexer.lex();
  if (!lexer.ok())
    return 1;

  if (stage == Stage::Lex) {
    print_tokens(lexer.get_tokens());
    return 0;
  }

  Parser parser(lexer.get_tokens(), source_path.string());
  auto program = parser.parse();
  if (!program)
    return 1;

  if (stage == Stage::Parse) {
    pretty_print(*program);
    return 0;
  }

  auto t0 = std::chrono::steady_clock::now();
  if (!resolve_labels(*program))
    return 1;
  auto t1 = std::chrono::steady_clock::now();
  if (!resolve_break_and_continue(*program))
    return 1;
  auto t2 = std::chrono::steady_clock::now();
  if (!resolve_identifiers(*program))
    return 1;
  auto t3 = std::chrono::steady_clock::now();
  if (!typecheck(*program))
    return 1;
  auto t4 = std::chrono::steady_clock::now();
  if (!resolve_switches(*program))
    return 1;
  auto t5 = std::chrono::steady_clock::now();

  if (std::getenv("MCC_TIMING"))
    std::println(
        "labels={}us breaks={}us identifiers={}us typecheck={}us switches={}us",
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count());

  if (stage == Stage::Validate)
    return 0;

  if (stage == Stage::Nir) {
    emit_nir(*program);
    return 0;
  }

  auto asm_program = codegen(*program);

  if (stage == Stage::CodeGen) {
    return 0;
  }

  emit_asm(asm_program, assembly);
  return 0;
}

static std::filesystem::path write_asm(const std::filesystem::path &source_path,
                                       const std::string &assembly) {
  auto asm_path = source_path.parent_path() /
                  (source_path.stem().string() + ".s");
  std::ofstream ofs(asm_path);
  ofs << assembly;
  return asm_path;
}

static int assemble(const std::filesystem::path &asm_path,
                    const std::filesystem::path &obj_path) {
  std::string cmd = "gcc -c " + asm_path.string() + " -o " + obj_path.string();
  return std::system(cmd.c_str());
}

int main(int argc, char *argv[]) {
  Stage stage = Stage::Run;
  std::vector<std::filesystem::path> sources;
  std::vector<std::string> linker_options;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "-lm") {
      linker_options.emplace_back(arg);
      continue;
    }
    if (!arg.starts_with('-')) {
      sources.emplace_back(arg);
      continue;
    }

    auto s = parse_stage(arg);
    if (!s) {
      std::println("Unknown option: {}", arg);
      return 1;
    }
    stage = *s;
  }

  if (sources.empty()) {
    std::println("Usage: mcc [--lex | --parse | --validate | --tacky | "
                 "--codegen | -S | -c] <file.c>...");
    return 1;
  }

  for (const auto &source_path : sources) {
    if (!std::filesystem::exists(source_path)) {
      std::println("error: file '{}' not found", source_path.string());
      return 1;
    }
  }

  std::vector<std::filesystem::path> objects;

  for (const auto &source_path : sources) {
    std::string assembly;
    if (compile_file(source_path, stage, assembly) != 0)
      return 1;

    switch (stage) {
      case Stage::Lex:
      case Stage::Parse:
      case Stage::Validate:
      case Stage::Nir:
      case Stage::CodeGen:
        continue;

      case Stage::EmitAsm:
        write_asm(source_path, assembly);
        continue;

      case Stage::Object: {
        auto asm_path = write_asm(source_path, assembly);
        auto obj_path = source_path.parent_path() /
                        (source_path.stem().string() + ".o");
        if (assemble(asm_path, obj_path) != 0)
          return 1;
        continue;
      }

      case Stage::Run: {
        auto asm_path = write_asm(source_path, assembly);
        auto obj_path = source_path.parent_path() /
                        (source_path.stem().string() + ".o");
        if (assemble(asm_path, obj_path) != 0)
          return 1;
        objects.push_back(obj_path);
        continue;
      }
    }
  }

  if (stage != Stage::Run)
    return 0;

  std::string cmd = "gcc -D SUPPRESS_WARNINGS";
  for (const auto &obj : objects)
    cmd += " " + obj.string();
  cmd += " -o " +
         (sources.front().parent_path() / sources.front().stem()).string();
  for (const auto &option : linker_options) cmd += " " + option;

  return std::system(cmd.c_str());
}
