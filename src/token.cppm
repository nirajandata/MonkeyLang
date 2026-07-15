module;

#include <string_view>
#include <cstdint>

export module token;

export enum class TokenType : uint32_t {
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
    Tilde,
    Hyphen,
    Decrement,
    Error,
    Eof
};

export struct Token {
    TokenType type;
    std::string_view text;
    uint32_t line;
};
