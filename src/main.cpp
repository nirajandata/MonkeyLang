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

enum class Stage : std::uint8_t { Lex, Parse, CodeGen, EmitAsm, Run };

static std::optional<Stage> parse_stage(std::string_view arg) {
    if (arg == "--lex")     return Stage::Lex;
    if (arg == "--parse")   return Stage::Parse;
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

static void pretty_print(const Program& program, int indent = 0) {
    std::string pad(indent * 2, ' ');
    std::println("{}Program(", pad);
    std::println("{}  Function(", pad);
    std::println("{}    name=\"{}\",", pad, program.function.name);

    const auto& ret = std::get<Return>(program.function.body);
    const auto& constant = std::get<Constant>(ret.value);

    std::println("{}    body=Return(", pad);
    std::println("{}      Constant({})", pad, constant.value);
    std::println("{}    )", pad);
    std::println("{}  )", pad);
    std::println("{})", pad);
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        std::println("Usage: mcc [--lex | --parse | --codegen | -S] <file.c>");
        return 1;
    }

    Stage stage;
    std::filesystem::path source_path;

    if (argc == 2) {
        stage = Stage::Run;
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

    switch (stage) {
        using enum Stage;
    case Lex: {
            Lexer lexer(source_path);
            lexer.lex();
            if (!lexer.ok()) return 1;
            print_tokens(lexer.get_tokens());
            return 0;
    }
    case Parse: {
            Lexer lexer(source_path);
            lexer.lex();
            if (!lexer.ok()) return 1;
            Parser parser(lexer.get_tokens(), source_path.string());
            auto program = parser.parse();
            if (!program) return 1;
            pretty_print(*program);
            return 0;
    }
    case CodeGen: {
            Lexer lexer(source_path);
            lexer.lex();
            if (!lexer.ok()) return 1;
            Parser parser(lexer.get_tokens(), source_path.string());
            auto program = parser.parse();
            if (!program) return 1;
            codegen(*program);
            return 0;
    }
    case EmitAsm: {
            Lexer lexer(source_path);
            lexer.lex();
            if (!lexer.ok()) return 1;
            Parser parser(lexer.get_tokens(), source_path.string());
            auto program = parser.parse();
            if (!program) return 1;
            auto asm_program = codegen(*program);
            std::string output;
            emit_asm(asm_program, output);
            auto asm_path = source_path.parent_path()
                          / (source_path.stem().string() + ".s");
            std::ofstream ofs(asm_path);
            ofs << output;
            return 0;
    }
    case Run: {
            Lexer lexer(source_path);
            lexer.lex();
            if (!lexer.ok()) return 1;
            Parser parser(lexer.get_tokens(), source_path.string());
            auto program = parser.parse();
            if (!program) return 1;
            auto asm_program = codegen(*program);
            std::string output;
            emit_asm(asm_program, output);
            auto stem = source_path.stem();
            auto dir = source_path.parent_path();
            auto asm_path = dir / (stem.string() + ".s");
            auto exe_path = dir / stem;
            {
                std::ofstream ofs(asm_path);
                ofs << output;
            }
            std::string cmd = "gcc -D SUPPRESS_WARNINGS " + asm_path.string()
                            + " -o " + exe_path.string() + " 2>/dev/null";
            int rc = std::system(cmd.c_str());
            std::filesystem::remove(asm_path);
            return rc;
    }
    }
}
