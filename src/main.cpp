#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <string_view>
#include <print>
#include <cstdlib>
#include <variant>
#include <meta>

import token;
import ast;
import lexer;
import parser;
import semantic;
import nir;
import codegen;

enum class Stage : std::uint8_t { Lex, Parse, Validate, Nir, CodeGen, EmitAsm, Run };

static std::optional<Stage> parse_stage(std::string_view arg) {
    if (arg == "--lex")      return Stage::Lex;
    if (arg == "--parse")    return Stage::Parse;
    if (arg == "--validate") return Stage::Validate;
    if (arg == "--tacky" || arg == "--nir") return Stage::Nir;
    if (arg == "--codegen")  return Stage::CodeGen;
    if (arg == "-S")        return Stage::EmitAsm;
    return std::nullopt;
}

static void print_tokens(const std::vector<Token>& tokens) {
    for (const auto& t : tokens) {
        if (t.type == TokenType::Eof) {
            std::println("EOF at line {}", t.line);
            continue;
        }
        std::println("Token: {:<2} | Line: {:<4} | Text: '{}'",
                     static_cast<uint32_t>(t.type),
                     t.line,
                     t.text);
    }
}

struct GetTypeName {
  template <typename T>
  constexpr std::string_view operator()(const T&) const {
    return std::meta::identifier_of(^^T);
  }
};
constexpr GetTypeName get_type_name{};

static void pretty_print(const Exp& exp, int indent = 0) {
  std::string pad(indent * 2, ' ');
  std::visit(Overload{
      [&](const Constant& c) {
          std::println("{}{}({})", pad, std::meta::identifier_of(^^Constant), c.value);
      },
      [&](const Var& v) {
          std::println("{}{}({})", pad, std::meta::identifier_of(^^Var), v.name);
      },
      [&](const Assignment& a) {
          std::println("{}{}(", pad, std::meta::identifier_of(^^Assignment));
          pretty_print(*a.left, indent + 2);
          pretty_print(*a.right, indent + 2);
          std::println("{})", pad);
      },
      [&](const CompoundAssignment& a) {
          std::string_view op_name = std::visit(get_type_name, a.op);
          std::println("{}{}(", pad, std::meta::identifier_of(^^CompoundAssignment));
          std::println("{}  {},", pad, op_name);
          pretty_print(*a.left, indent + 2);
          pretty_print(*a.right, indent + 2);
          std::println("{})", pad);
      },
      [&](const IncDec& e) {
          std::string_view op_name = std::visit(get_type_name, e.op);
          std::println("{}{}(", pad, std::meta::identifier_of(^^IncDec));
          std::println("{}  {},", pad, op_name);
          std::println("{}  {},", pad, e.postfix ? "postfix" : "prefix");
          pretty_print(*e.exp, indent + 2);
          std::println("{})", pad);
      },
      [&](const Unary& u) {
          std::string_view op_name = std::visit(get_type_name, u.op);

          std::println("{}{}(", pad, std::meta::identifier_of(^^Unary));
          std::println("{}  {},", pad, op_name);
          pretty_print(*u.exp, indent + 2);
          std::println("{})", pad);
      },
      [&](const Binary& b) {
          std::string_view op_name = std::visit(get_type_name, b.op);

          std::println("{}{}(", pad, std::meta::identifier_of(^^Binary));
          std::println("{}  {},", pad, op_name);
          pretty_print(*b.left, indent + 2);
          pretty_print(*b.right, indent + 2);
          std::println("{})", pad);
      },
      [&](const Conditional& c) {
          std::println("{}{}(", pad, std::meta::identifier_of(^^Conditional));
          pretty_print(*c.condition, indent + 2);
          pretty_print(*c.then_exp, indent + 2);
          pretty_print(*c.else_exp, indent + 2);
          std::println("{})", pad);
      },
  }, exp.value);
}

static void pretty_print(const Statement& stmt, int indent = 0) {
  std::string pad(indent * 2, ' ');

  std::visit(Overload{
      [&](const Return& r) {
        std::println("{}{}(", pad, std::meta::identifier_of(^^Return));
        pretty_print(r.value, indent + 2);
        std::println("{})", pad);
      },
      [&](const Expression& e) {
        std::println("{}{}(", pad, std::meta::identifier_of(^^Expression));
        pretty_print(e.value, indent + 2);
        std::println("{})", pad);
      },
      [&](const Null&) {
        std::println("{}{}", pad, std::meta::identifier_of(^^Null));
      },
      [&](const If& i) {
        std::println("{}{}(", pad, std::meta::identifier_of(^^If));
        pretty_print(i.condition, indent + 2);
        pretty_print(*i.then_stmt, indent + 2);
        if (i.else_stmt) pretty_print(*i.else_stmt, indent + 2);
        std::println("{})", pad);
      },
      [&](const Goto& g) {
        std::println("{}{}(\"{}\")", pad, std::meta::identifier_of(^^Goto), g.label);
      },
      [&](const Label& l) {
        std::println("{}{}(\"{}\"", pad, std::meta::identifier_of(^^Label), l.name);
        pretty_print(*l.stmt, indent + 2);
        std::println("{})", pad);
      },
  }, stmt.value);
}

static void pretty_print(const Program& program, int indent = 0) {
  std::string pad(indent * 2, ' ');

  std::println("{}{}(", pad, std::meta::identifier_of(^^Program));
  std::println("{}  {}(", pad, std::meta::identifier_of(^^Function));
  std::println("{}    name=\"{}\",", pad, program.function.name);
  std::println("{}    body=[", pad);

  for (const auto& item : program.function.body) {
    std::visit(Overload{
        [&](const Statement& s) {
          pretty_print(s, indent + 3);
        },
        [&](const Declaration& d) {
          std::println("{}      {}(name=\"{}\"", pad, std::meta::identifier_of(^^Declaration), d.name);
          if (d.init) pretty_print(*d.init, indent + 4);
          std::println("{}      )", pad);
        },
    }, item);
  }

  std::println("{}    ]", pad);
  std::println("{}  )", pad);
  std::println("{})", pad);
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        std::println("Usage: mcc [--lex | --parse | --validate | --tacky | --codegen | -S] <file.c>");
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
    if (!lexer.ok()) return 1;

    if (stage == Stage::Lex) {
        print_tokens(lexer.get_tokens());
        return 0;
    }

    Parser parser(lexer.get_tokens(), source_path.string());
    auto program = parser.parse();
    if (!program) return 1;

    if (stage == Stage::Parse) {
        pretty_print(*program);
        return 0;
    }

    if (!resolve_labels(*program)) return 1;

    if (!resolve_variables(*program)) return 1;

    if (stage == Stage::Validate) return 0;

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
    std::string cmd = "gcc -D SUPPRESS_WARNINGS " + asm_path.string()
                    + " -o " + exe_path.string() + " 2>/dev/null";

    int rc = std::system(cmd.c_str());
    //std::filesystem::remove(asm_path);
    return rc;
}

