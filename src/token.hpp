#pragma once

#include <string_view>
#include <cstdint>

enum class TokenType : uint32_t {
    Identifier = 1,
    Constant,
    Int,
    Void,
    Return,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Semicolon,
    Slash,
    Error,
    Eof
};

struct Token {
    TokenType type;
    std::string_view text;
    uint32_t line;
};