#pragma once

#include <string>
#include <vector>
#include <string_view>
#include <fstream>
#include <filesystem>
#include <cctype>

enum Token : uint32_t
{
    T_IDENTIFIER = 1,
    T_CONSTANT,
    T_INT,
    T_VOID,
    T_RETURN,
    T_LPAREN,
    T_RPAREN,
    T_LBRACE,
    T_RBRACE,
    T_SEMICOLON,
};

class Lexer
{
    static constexpr size_t BUFFER_SIZE = 1 << 15;
    std::ifstream file_;
    std::vector<Token> tokens_;
    char buffer_[BUFFER_SIZE];
    char *cursor_;
    char *limit_;

    bool had_error_ = false;

    bool refill()
    {
        if (cursor_ < limit_)
            return true;

        file_.read(buffer_, BUFFER_SIZE);
        limit_ = buffer_ + file_.gcount();
        cursor_ = buffer_;
        return cursor_ < limit_;
    }

    char peek()
    {
        return *cursor_;
    }

    void advance()
    {
        ++cursor_;
    }

    void skip_whitespace()
    {
        while (refill() && std::isspace(peek()))
            advance();
    }

    void skip_line_comment()
    {
        while (refill() && peek() != '\n')
            advance();
    }

    void skip_block_comment()
    {
        while (refill())
        {
            if (peek() == '*')
            {
                advance();
                if (refill() && peek() == '/')
                {
                    advance();
                    return;
                }
            }
            else
            {
                advance();
            }
        }
        had_error_ = true;
    }

    Token read_identifier_or_keyword()
    {
        std::string word;
        while (refill() && (std::isalnum(peek()) || peek() == '_'))
        {
            word += peek();
            advance();
        }

        if (word == "int")    return T_INT;
        if (word == "void")   return T_VOID;
        if (word == "return") return T_RETURN;
        return T_IDENTIFIER;
    }

    Token read_constant()
    {
        while (refill() && std::isdigit(peek()))
            advance();
        if (refill() && (std::isalnum(peek()) || peek() == '_'))
        {
            had_error_ = true;
            while (refill() && (std::isalnum(peek()) || peek() == '_'))
                advance();
            return static_cast<Token>(0);
        }
        return T_CONSTANT;
    }

    Token read_single_char()
    {
        char c = peek();
        advance();
        switch (c)
        {
            case '(': return T_LPAREN;
            case ')': return T_RPAREN;
            case '{': return T_LBRACE;
            case '}': return T_RBRACE;
            case ';': return T_SEMICOLON;
            default:
                had_error_ = true;
                return static_cast<Token>(0);
        }
    }

public:
    Lexer(std::filesystem::path source_file)
        : file_{source_file}, cursor_{nullptr}, limit_{nullptr} {}

    void lex()
    {
        tokens_.clear();
        had_error_ = false;
        file_.clear();
        file_.seekg(0);
        cursor_ = limit_ = nullptr;

        while (refill())
        {
            skip_whitespace();
            if (!refill()) break;

            if (std::isalpha(peek()) || peek() == '_')
                tokens_.push_back(read_identifier_or_keyword());
            else if (std::isdigit(peek()))
                tokens_.push_back(read_constant());
            else if (peek() == '/')
            {
                advance();
                if (!refill() || (peek() != '/' && peek() != '*'))
                {
                    had_error_ = true;
                    break;
                }
                if (peek() == '/')
                    skip_line_comment();
                else
                    skip_block_comment();
            }
            else
                tokens_.push_back(read_single_char());
        }
    }

    bool ok() const { return !had_error_; }

    std::vector<Token> const& get_tokens() const { return tokens_; }
};
