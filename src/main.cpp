#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <meta>
#include <print>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import token;
import ast;
import lexer;
import parser;
import semantic;
import nir;
import codegen;

enum class Stage : std::uint8_t {
  Lex,
  Parse,
  Validate,
  Nir,
  CodeGen,
  EmitAsm,
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

struct GetTypeName {
  template <typename T> constexpr std::string_view operator()(const T &) const {
    return std::meta::identifier_of(^^T);
  }
};
constexpr GetTypeName get_type_name{};

static std::string join_names(const std::vector<std::string> &names) {
  std::string joined;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i) joined += ", ";
    joined += names[i];
  }
  return joined;
}

static void pretty_print(const Block &block, int indent = 0);
static void pretty_print(const Exp &exp, int indent = 0);
static void pretty_print(const VariableDeclaration &d, int indent = 0);
static void pretty_print(const Declaration &d, int indent = 0);
static void pretty_print(const ForInit &init, int indent = 0);

static void pretty_print(const VariableDeclaration &d, int indent) {
  std::string pad(indent * 2, ' ');
  std::println("{}{}(name=\"{}\"", pad,
               std::meta::identifier_of(^^VariableDeclaration), d.name);
  if (d.init)
    pretty_print(*d.init, indent + 2);
  std::println("{})", pad);
}

static void pretty_print(const Declaration &d, int indent) {
  std::string pad(indent * 2, ' ');

  std::println("{}{}(", pad, std::meta::identifier_of(^^Declaration));
  std::visit(
      Overload{
          [&](const VarDecl &v) {
            pretty_print(v.decl, indent + 2);
          },
          [&](const FunDecl &f) {
            std::println("{}  {}(", pad, std::meta::identifier_of(^^FunDecl));
            std::println("{}    name=\"{}\",", pad, f.decl.name);
            std::println("{}    params=[{}],", pad, join_names(f.decl.params));
            if (f.decl.body)
              pretty_print(*f.decl.body, indent + 4);
            std::println("{}  )", pad);
          },
      },
      d);
  std::println("{})", pad);
}

static void pretty_print(const ForInit &init, int indent) {
  std::string pad(indent * 2, ' ');

  std::visit(
      Overload{
          [&](const InitDecl &d) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^InitDecl));
            pretty_print(d.decl, indent + 2);
            std::println("{})", pad);
          },
          [&](const InitExp &e) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^InitExp));
            if (e.exp)
              pretty_print(*e.exp, indent + 2);
            std::println("{})", pad);
          },
      },
      init);
}

static void pretty_print(const Exp &exp, int indent) {
  std::string pad(indent * 2, ' ');
  std::visit(
      Overload{
          [&](const Constant &c) {
            std::println("{}{}({})", pad, std::meta::identifier_of(^^Constant),
                         c.value);
          },
          [&](const Var &v) {
            std::println("{}{}({})", pad, std::meta::identifier_of(^^Var),
                         v.name);
          },
          [&](const Assignment &a) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^Assignment));
            pretty_print(*a.left, indent + 2);
            pretty_print(*a.right, indent + 2);
            std::println("{})", pad);
          },
          [&](const CompoundAssignment &a) {
            std::string_view op_name = std::visit(get_type_name, a.op);
            std::println("{}{}(", pad,
                         std::meta::identifier_of(^^CompoundAssignment));
            std::println("{}  {},", pad, op_name);
            pretty_print(*a.left, indent + 2);
            pretty_print(*a.right, indent + 2);
            std::println("{})", pad);
          },
          [&](const IncDec &e) {
            std::string_view op_name = std::visit(get_type_name, e.op);
            std::println("{}{}(", pad, std::meta::identifier_of(^^IncDec));
            std::println("{}  {},", pad, op_name);
            std::println("{}  {},", pad, e.postfix ? "postfix" : "prefix");
            pretty_print(*e.exp, indent + 2);
            std::println("{})", pad);
          },
          [&](const Unary &u) {
            std::string_view op_name = std::visit(get_type_name, u.op);

            std::println("{}{}(", pad, std::meta::identifier_of(^^Unary));
            std::println("{}  {},", pad, op_name);
            pretty_print(*u.exp, indent + 2);
            std::println("{})", pad);
          },
          [&](const Binary &b) {
            std::string_view op_name = std::visit(get_type_name, b.op);

            std::println("{}{}(", pad, std::meta::identifier_of(^^Binary));
            std::println("{}  {},", pad, op_name);
            pretty_print(*b.left, indent + 2);
            pretty_print(*b.right, indent + 2);
            std::println("{})", pad);
          },
          [&](const Conditional &c) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^Conditional));
            pretty_print(*c.condition, indent + 2);
            pretty_print(*c.then_exp, indent + 2);
            pretty_print(*c.else_exp, indent + 2);
            std::println("{})", pad);
          },
          [&](const FunctionCall &c) {
            std::println("{}{}(\"{}\"", pad,
                         std::meta::identifier_of(^^FunctionCall), c.name);
            for (const auto &arg : c.args) pretty_print(*arg, indent + 2);
            std::println("{})", pad);
          },
      },
      exp.value);
}

static void pretty_print(const Expression &e, int indent = 0) {
  pretty_print(e.value, indent);
}

static void pretty_print(const Statement &stmt, int indent = 0) {
  std::string pad(indent * 2, ' ');

  std::visit(
      Overload{
          [&](const Return &r) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^Return));
            pretty_print(r.value, indent + 2);
            std::println("{})", pad);
          },
          [&](const Expression &e) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^Expression));
            pretty_print(e.value, indent + 2);
            std::println("{})", pad);
          },
          [&](const Null &) {
            std::println("{}{}", pad, std::meta::identifier_of(^^Null));
          },
          [&](const If &i) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^If));
            pretty_print(i.condition, indent + 2);
            pretty_print(*i.then_stmt, indent + 2);
            if (i.else_stmt)
              pretty_print(*i.else_stmt, indent + 2);
            std::println("{})", pad);
          },
          [&](const Goto &g) {
            std::println("{}{}(\"{}\")", pad, std::meta::identifier_of(^^Goto),
                         g.label);
          },
          [&](const Label &l) {
            std::println("{}{}(\"{}\"", pad, std::meta::identifier_of(^^Label),
                         l.name);
            pretty_print(*l.stmt, indent + 2);
            std::println("{})", pad);
          },
          [&](const Compound &c) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^Compound));
            pretty_print(*c.block, indent + 2);
            std::println("{})", pad);
          },
          [&](const Break &b) {
            std::println("{}{}(\"{}\")", pad, std::meta::identifier_of(^^Break),
                         b.label);
          },
          [&](const Continue &c) {
            std::println("{}{}(\"{}\")", pad,
                         std::meta::identifier_of(^^Continue), c.label);
          },
          [&](const Case &c) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^Case));
            pretty_print(c.value, indent + 2);
            std::println("{})", pad);
          },
          [&](const Default &) {
            std::println("{}{}", pad, std::meta::identifier_of(^^Default));
          },
          [&](const Switch &s) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^Switch));
            pretty_print(s.condition, indent + 2);
            pretty_print(*s.body, indent + 2);
            std::println("{})", pad);
          },
          [&](const While &w) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^While));
            pretty_print(*w.condition, indent + 2);
            pretty_print(*w.body, indent + 2);
            std::println("{})", pad);
          },
          [&](const DoWhile &d) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^DoWhile));
            pretty_print(*d.body, indent + 2);
            pretty_print(*d.condition, indent + 2);
            std::println("{})", pad);
          },
          [&](const For &f) {
            std::println("{}{}(", pad, std::meta::identifier_of(^^For));
            pretty_print(f.init, indent + 2);
            if (f.condition)
              pretty_print(*f.condition, indent + 2);
            if (f.post)
              pretty_print(*f.post, indent + 2);
            pretty_print(*f.body, indent + 2);
            std::println("{})", pad);
          },
      },
      stmt.value);
}

static void pretty_print(const Block &block, int indent) {
  std::string pad(indent * 2, ' ');

  std::println("{}{}(", pad, std::meta::identifier_of(^^Block));
  for (const auto &item : block.items) {
    std::visit(Overload{
                   [&](const Statement &s) { pretty_print(s, indent + 1); },
                   [&](const Declaration &d) { pretty_print(d, indent); },
               },
               item);
  }
  std::println("{})", pad);
}

static void pretty_print(const Program &program, int indent = 0) {
  std::string pad(indent * 2, ' ');

  std::println("{}{}(", pad, std::meta::identifier_of(^^Program));
  for (const auto &func : program.functions) {
    std::println("{}  {}(", pad,
                 std::meta::identifier_of(^^FunctionDeclaration));
    std::println("{}    name=\"{}\",", pad, func.name);
    std::println("{}    params=[{}],", pad, join_names(func.params));
    if (func.body)
      pretty_print(*func.body, indent + 2);
    std::println("{}  )", pad);
  }
  std::println("{})", pad);
}

int main(int argc, char *argv[]) {
  if (argc < 2 || argc > 3) {
    std::println("Usage: mcc [--lex | --parse | --validate | --tacky | "
                 "--codegen | -S] <file.c>");
    return 1;
  }

  Stage stage = Stage::Run;
  std::filesystem::path source_path;

  if (argc == 2) {
    source_path = argv[1];
  } else {
    auto s = parse_stage(argv[1]);
    if (!s) {
      std::println("Unknown option: {}", argv[1]);
      return 1;
    }
    stage = *s;
    source_path = argv[2];
  }

  if (!std::filesystem::exists(source_path)) {
    std::println("error: file '{}' not found", source_path.string());
    return 1;
  }

  Lexer lexer(source_path);
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
  if (!resolve_variables(*program))
    return 1;
  auto t3 = std::chrono::steady_clock::now();
  if (!resolve_switches(*program))
    return 1;
  auto t4 = std::chrono::steady_clock::now();
  if (std::getenv("MCC_TIMING"))
    std::println(
        "labels={}us breaks={}us variables={}us switches={}us",
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count());

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

  std::string output;
  emit_asm(asm_program, output);

  auto stem = source_path.stem();
  auto dir = source_path.parent_path();
  auto asm_path = dir / (stem.string() + ".s");

  {
    std::ofstream ofs(asm_path);
    ofs << output;
  }

  if (stage == Stage::EmitAsm) {
    return 0;
  }

  auto exe_path = dir / stem;
  std::string cmd = "gcc -D SUPPRESS_WARNINGS " + asm_path.string() + " -o " +
                    exe_path.string() + " 2>/dev/null";

  int rc = std::system(cmd.c_str());
  // std::filesystem::remove(asm_path);
  return rc;
}
