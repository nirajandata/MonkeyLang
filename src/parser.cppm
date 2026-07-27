module;

#include <charconv>
#include <memory>
#include <string_view>
#include <vector>
#include <print>

export module parser;

import token;
import ast;

export class Parser {
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

        int32_t value = 0;
        const auto [ptr, ec] =
            std::from_chars(tok.text.data(), tok.text.data() + tok.text.size(), value);

        if (ec != std::errc{} || ptr != tok.text.data() + tok.text.size()) {
            std::println("{} a valid integer constant  {} ", tok.line,tok.text);
            return {.value = 0, .line = tok.line};
        }
        return {.value = value, .line = tok.line};
    }

    Exp parse_primary() {
        auto tok = peek();

        if (tok.type == TokenType::Constant) {
            return Exp{parse_constant()};
        }

        if (tok.type == TokenType::Tilde || tok.type == TokenType::Hyphen
            || tok.type == TokenType::Bang) {
            advance();
            uint32_t line = tok.line;
            UnaryOp op = (tok.type == TokenType::Tilde)
                ? UnaryOp{Complement{}}
                : (tok.type == TokenType::Hyphen)
                    ? UnaryOp{Negate{}}
                    : UnaryOp{Not{}};
            auto inner = parse_primary();
            return Exp{Unary{std::move(op), std::make_unique<Exp>(std::move(inner)), line}};
        }

        if (tok.type == TokenType::LParen) {
            advance();
            auto inner = parse_exp(0);
            expect(TokenType::RParen, "\")\"");
            return inner;
        }

        if (tok.type == TokenType::Decrement) {
            std::println("error:{}: Unexpected '--'", tok.line);
            had_error_ = true;
            advance();
            return Exp{Constant{0, tok.line}};
        }

        std::println("error:{}: Expected expression but found '{}'",
                     tok.line, tok.text);
        had_error_ = true;
        advance();
        return Exp{Constant{0, tok.line}};
    }

    static int binary_prec(TokenType type) {
        switch (type) {
            case TokenType::BarBar:                   return 5;
            case TokenType::AmpAmp:                   return 10;
            case TokenType::Bar:                      return 15;
            case TokenType::Caret:                    return 20;
            case TokenType::Amp:                      return 25;
            case TokenType::EqEq:
            case TokenType::BangEq:                   return 30;
            case TokenType::Lt:
            case TokenType::LtEq:
            case TokenType::Gt:
            case TokenType::GtEq:                     return 35;
            case TokenType::LtLt:
            case TokenType::GtGt:                     return 40;
            case TokenType::Plus:
            case TokenType::Hyphen:                   return 45;
            case TokenType::Star:
            case TokenType::Slash:
            case TokenType::Percent:                  return 50;
            default:                                  return 0;
        }
    }

    static BinaryOp token_to_binop(TokenType type) {
        switch (type) {
            case TokenType::Plus:    return BinaryOp{Add{}};
            case TokenType::Hyphen:  return BinaryOp{Subtract{}};
            case TokenType::Star:    return BinaryOp{Multiply{}};
            case TokenType::Slash:   return BinaryOp{Divide{}};
            case TokenType::Percent: return BinaryOp{Remainder{}};
            case TokenType::AmpAmp:  return BinaryOp{And{}};
            case TokenType::BarBar:  return BinaryOp{Or{}};
            case TokenType::EqEq:    return BinaryOp{Equal{}};
            case TokenType::BangEq:  return BinaryOp{NotEqual{}};
            case TokenType::Lt:      return BinaryOp{LessThan{}};
            case TokenType::LtEq:    return BinaryOp{LessOrEqual{}};
            case TokenType::Gt:      return BinaryOp{GreaterThan{}};
            case TokenType::GtEq:    return BinaryOp{GreaterOrEqual{}};
            case TokenType::Amp:     return BinaryOp{BitwiseAnd{}};
            case TokenType::Bar:     return BinaryOp{BitwiseOr{}};
            case TokenType::Caret:   return BinaryOp{BitwiseXor{}};
            case TokenType::LtLt:    return BinaryOp{ShiftLeft{}};
            case TokenType::GtGt:    return BinaryOp{ShiftRight{}};
            default:                 return BinaryOp{Add{}};
        }
    }

    Exp parse_exp(int min_prec) {
        auto left = parse_primary();

        while (true) {
            int prec = binary_prec(peek().type);
            if (prec == 0 || prec < min_prec) break;

            auto tok = advance();
            auto op = token_to_binop(tok.type);
            auto right = parse_exp(prec + 1);

            left = Exp{Binary{std::move(op),
                              std::make_unique<Exp>(std::move(left)),
                              std::make_unique<Exp>(std::move(right)),
                              tok.line}};
        }

        return left;
    }

    Return parse_return() {
        uint32_t line = peek().line;
        expect(TokenType::Return, "\"return\"");
        auto val = parse_exp(0);
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
