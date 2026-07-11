#pragma once

#include <string_view>
#include <vector>
#include <print>
#include "token.hpp"
#include "ast.hpp"

class Parser {
    const std::vector<Token>& tokens_;
    size_t pos_ = 0;
    bool had_error_ = false;
    std::string_view filename_;

    const Token& peek() const { return tokens_[pos_]; }

    Token advance() { return tokens_[pos_++]; }

    bool check(TokenType type) const { return peek().type == type; }

    void expect(TokenType type, std::string_view expected) {
        if (!check(type)) {
            std::println("error:{}: Expected {} but found '{}'",
                         peek().line, expected, peek().text);
            had_error_ = true;
        } else {
            advance();
        }
    }

    Constant parse_constant() {
        const Token& tok = advance();
        if (tok.type != TokenType::Constant) {
            std::println("error:{}: Expected constant but found '{}'",
                         tok.line, tok.text);
            had_error_ = true;
            return {0, tok.line};
        }

        int32_t val = 0;
        for (char c : tok.text) {
            val = val * 10 + (c - '0');
        }
        return {val, tok.line};
    }

    Exp parse_exp() {
        return parse_constant();
    }

    Return parse_return() {
        uint32_t line = peek().line;
        expect(TokenType::Return, "\"return\"");
        auto val = parse_exp();
        expect(TokenType::Semicolon, "\";\"");
        return {std::move(val), line};
    }

    Statement parse_statement() {
        return parse_return();
    }

    Function parse_function() {
        uint32_t line = peek().line;
        expect(TokenType::Int, "\"int\"");

        const Token& name_tok = advance();
        if (name_tok.type != TokenType::Identifier) {
            std::println("error:{}: Expected identifier but found '{}'",
                         name_tok.line, name_tok.text);
            had_error_ = true;
        }
        std::string_view name = name_tok.text;

        expect(TokenType::LParen, "\"(\"");
        expect(TokenType::Void, "\"void\"");
        expect(TokenType::RParen, "\")\"");
        expect(TokenType::LBrace, "\"{\"");

        auto body = parse_statement();

        expect(TokenType::RBrace, "\"}\"");

        return {name, std::move(body), line};
    }

    Program parse_program() {
        auto func = parse_function();

        if (!check(TokenType::Eof)) {
            std::println("error:{}: Expected end of file but found '{}'",
                         peek().line, peek().text);
            had_error_ = true;
        }

        return {std::move(func)};
    }

public:
    Parser(const std::vector<Token>& tokens, std::string_view filename)
        : tokens_(tokens), filename_(filename) {}

    std::optional<Program> parse() {
        auto program = parse_program();
        if (had_error_) return std::nullopt;
        return program;
    }

    bool ok() const { return !had_error_; }
};
