#include <filesystem>
#include <vector>
#include <string_view>
#include <print>
#include <cstdlib>
#include "lexer.hpp"
#include "parser.hpp"

enum class Stage : std::uint8_t { Lex, Parse, CodeGen, EmitAsm };

static Stage parse_stage(std::string_view arg) {
    if (arg == "--lex")     return Stage::Lex;
    if (arg == "--parse")   return Stage::Parse;
    if (arg == "--codegen") return Stage::CodeGen;
    if (arg == "-S")        return Stage::EmitAsm;

    std::println("Unknown option: {}", arg);
    std::exit(1);
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
    if (argc != 3) {
        std::println("Usage: mcc <--lex | --parse | --codegen | -S> <file.c>");
        return 1;
    }

    std::filesystem::path source_path{argv[2]};
    if (!std::filesystem::exists(source_path)) {
        std::println("error: file '{}' not found", source_path.string());
        return 1;
    }

    Stage stage = parse_stage(argv[1]);

    switch (stage) {
        using enum Stage;
    case Lex: {
            Lexer lexer(source_path);
            lexer.lex();
            if (!lexer.ok()) {
                std::println("error: lexer encountered invalid tokens");
                return 1;
            }
            print_tokens(lexer.get_tokens());
            return 0;
    }
    case Parse: {
            Lexer lexer(source_path);
            lexer.lex();
            if (!lexer.ok()) {
                std::println("error: lexer encountered invalid tokens");
                return 1;
            }
            Parser parser(lexer.get_tokens(), source_path.string());
            auto program = parser.parse();
            if (!program) {
                return 1;
            }
            pretty_print(*program);
            return 0;
    }
    case CodeGen:
        return 0;
    case EmitAsm:
        return 0;
    }
}