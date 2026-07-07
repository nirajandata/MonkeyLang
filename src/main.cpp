#include <filesystem>
#include <iostream>
#include <string_view>

enum class Stage : std::uint8_t { Lex, Parse, CodeGen, EmitAsm };

Stage parser(std::string_view arg) {
  if (arg == "--lex")
    return Stage::Lex;
  if (arg == "--parse")
    return Stage::Parse;
  if (arg == "--codegen")
    return Stage::CodeGen;
  if (arg == "-S")
    return Stage::EmitAsm;
  std::exit(1);
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr
        << "Usage: mcc (--lex |  --parse  | --codegen | -S filename.c ) \n";
    return 1;
  }
  std::filesystem::path source_path{argv[1]};
  if (!std::filesystem::exists(source_path)) {
    return 1;
  }

  Stage option{parser(argv[1])};

  switch (option) {
    using enum Stage;
  case Lex:
    return 0;
  case Parse:
    return 0;
  case CodeGen:
    return 0;
  case EmitAsm:
    return 0;
  }
}
