#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <string_view>
#include <print>
#include <cstdlib>
#include <variant>

import token;
import ast;
import lexer;
import parser;
import codegen;

enum class Stage : std::uint8_t { Lex, Parse, Tacky, CodeGen, EmitAsm, Run };

static std::optional<Stage> parse_stage(std::string_view arg) {
    if (arg == "--lex")     return Stage::Lex;
    if (arg == "--parse")   return Stage::Parse;
    if (arg == "--tacky")   return Stage::Tacky;
    if (arg == "--codegen") return Stage::CodeGen;
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

static void pretty_print(const Exp& exp, int indent = 0) {
    std::string pad(indent * 2, ' ');
    std::visit(Overload{
        [&](const Constant& c) {
            std::println("{}Constant({})", pad, c.value);
        },
        [&](const Unary& u) {
            std::string op_name = std::visit(Overload{
                [](const Complement&) -> std::string { return "Complement"; },
                [](const Negate&) -> std::string { return "Negate"; },
            }, u.op);
            std::println("{}Unary(", pad);
            std::println("{}  {},", pad, op_name);
            pretty_print(*u.exp, indent + 2);
            std::println("{})", pad);
        },
    }, exp.value);
}

static void pretty_print(const Program& program, int indent = 0) {
    std::string pad(indent * 2, ' ');
    std::println("{}Program(", pad);
    std::println("{}  Function(", pad);
    std::println("{}    name=\"{}\",", pad, program.function.name);

    const auto& ret = std::get<Return>(program.function.body);

    std::println("{}    body=Return(", pad);
    pretty_print(ret.value, indent + 4);
    std::println("{}    )", pad);
    std::println("{}  )", pad);
    std::println("{})", pad);
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        std::println("Usage: mcc [--lex | --parse | --tacky | --codegen | -S] <file.c>");
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

    if (stage == Stage::Tacky) {
        emit_tacky(*program);
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

