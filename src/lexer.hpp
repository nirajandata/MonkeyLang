#pragma once

#include <string>
#include <vector>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <immintrin.h>
#include <bit>
#include "token.hpp"
#include "ascii.hpp"

class Lexer
{
    std::string source_;
    const char* cursor_;
    const char* limit_;
    std::vector<Token> tokens_;
    uint32_t current_line_ = 1;
    bool had_error_ = false;

    void skip_whitespace()
    {
        while (cursor_ + 64 <= limit_)
        {
            __m512i chars = _mm512_loadu_si512(cursor_);

            uint64_t ws = ascii::is_space_512(chars);
            __mmask64 nl = _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('\n'));

            if (ws == ~0ULL)
            {
                current_line_ += std::popcount(static_cast<uint64_t>(nl));
                cursor_ += 64;
            }
            else
            {
                uint64_t not_ws = ~ws;
                int offset = std::countr_zero(not_ws);

                uint64_t nl_skipped = nl & ((1ULL << offset) - 1);
                current_line_ += std::popcount(nl_skipped);

                cursor_ += offset;
                return;
            }
        }

        while (cursor_ < limit_ && ascii::is_space(*cursor_))
        {
            if (*cursor_ == '\n')
            {
                current_line_++;
            }
            cursor_++;
        }
    }

    void skip_line_comment()
    {
        while (cursor_ + 64 <= limit_)
        {
            __m512i chars = _mm512_loadu_si512(cursor_);
            __mmask64 nl = _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('\n'));

            if (nl == 0)
            {
                cursor_ += 64;
            }
            else
            {
                int offset = std::countr_zero(static_cast<uint64_t>(nl));
                cursor_ += offset;
                return;
            }
        }

        while (cursor_ < limit_ && *cursor_ != '\n')
        {
            cursor_++;
        }
    }

    void skip_block_comment()
    {
        while (cursor_ + 64 <= limit_)
        {
            __m512i chars = _mm512_loadu_si512(cursor_);
            __mmask64 star = _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('*'));
            __mmask64 nl = _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('\n'));

            if (star == 0)
            {
                current_line_ += std::popcount(static_cast<uint64_t>(nl));
                cursor_ += 64;
            }
            else
            {
                break;
            }
        }

        while (cursor_ < limit_)
        {
            if (*cursor_ == '\n')
            {
                current_line_++;
            }

            if (*cursor_ == '*')
            {
                cursor_++;
                if (cursor_ < limit_ && *cursor_ == '/')
                {
                    cursor_++;
                    return;
                }
            }
            else
            {
                cursor_++;
            }
        }

        had_error_ = true;
    }


    Token read_identifier_or_keyword()
    {
        const char* start = cursor_;

        while (cursor_ + 64 <= limit_)
        {
            __m512i chars = _mm512_loadu_si512(cursor_);

            __mmask64 is_under = _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('_'));
            uint64_t valid = ascii::is_alnum_512(chars) | is_under;

            if (valid == ~0ULL)
            {
                cursor_ += 64;
            }
            else
            {
                int offset = std::countr_zero(~valid);
                cursor_ += offset;
                break;
            }
        }

        while (cursor_ < limit_ && (ascii::is_alnum(*cursor_) || *cursor_ == '_'))
        {
            cursor_++;
        }

        std::string_view text(start, cursor_ - start);
        TokenType type = TokenType::Identifier;

        if (text == "int") type = TokenType::Int;
        else if (text == "void") type = TokenType::Void;
        else if (text == "return") type = TokenType::Return;

        return {type, text, current_line_};
    }

    Token read_constant()
    {
        const char* start = cursor_;

        while (cursor_ + 64 <= limit_)
        {
            __m512i chars = _mm512_loadu_si512(cursor_);

            uint64_t is_digit = ascii::is_digit_512(chars);

            if (is_digit == ~0ULL)
            {
                cursor_ += 64;
            }
            else
            {
                int offset = std::countr_zero(~is_digit);
                cursor_ += offset;
                break;
            }
        }

        while (cursor_ < limit_ && ascii::is_digit(*cursor_))
        {
            cursor_++;
        }

        if (cursor_ < limit_ && (ascii::is_alpha(*cursor_) || *cursor_ == '_'))
        {
            had_error_ = true;
            while (cursor_ < limit_ && (ascii::is_alnum(*cursor_) || *cursor_ == '_'))
            {
                cursor_++;
            }
            return {TokenType::Error, std::string_view(start, cursor_ - start), current_line_};
        }

        return {TokenType::Constant, std::string_view(start, cursor_ - start), current_line_};
    }

    Token read_single_char()
    {
        char c = *cursor_;
        const char* start = cursor_;
        cursor_++;

        switch (c)
        {
        case '(': return {TokenType::LParen, std::string_view(start, 1), current_line_};
        case ')': return {TokenType::RParen, std::string_view(start, 1), current_line_};
        case '{': return {TokenType::LBrace, std::string_view(start, 1), current_line_};
        case '}': return {TokenType::RBrace, std::string_view(start, 1), current_line_};
        case ';': return {TokenType::Semicolon, std::string_view(start, 1), current_line_};
        default:
            had_error_ = true;
            return {TokenType::Error, std::string_view(start, 1), current_line_};
        }
    }

public:
    Lexer(const std::filesystem::path& path) : cursor_{nullptr}, limit_{nullptr}
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (file.is_open())
        {
            auto size = file.tellg();
            source_.resize(size);
            file.seekg(0);
            file.read(source_.data(), size);
        }
        cursor_ = source_.data();
        limit_ = source_.data() + source_.size();
    }

    void lex()
    {
        tokens_.clear();
        had_error_ = false;
        current_line_ = 1;

        while (cursor_ < limit_)
        {
            skip_whitespace();
            if (cursor_ >= limit_) break;

            if (ascii::is_alpha(*cursor_) || *cursor_ == '_')
            {
                tokens_.push_back(read_identifier_or_keyword());
            }
            else if (ascii::is_digit(*cursor_))
            {
                tokens_.push_back(read_constant());
            }
            else if (*cursor_ == '/')
            {
                const char* start = cursor_;
                cursor_++;
                if (cursor_ < limit_ && *cursor_ == '/')
                {
                    cursor_++;
                    skip_line_comment();
                }
                else if (cursor_ < limit_ && *cursor_ == '*')
                {
                    cursor_++;
                    skip_block_comment();
                }
                else
                {
                    tokens_.push_back({TokenType::Slash, std::string_view(start, 1), current_line_});
                }
            }
            else
            {
                tokens_.push_back(read_single_char());
            }
        }
        tokens_.push_back({TokenType::Eof, "", current_line_});
    }

    bool ok() const { return !had_error_; }
    const std::vector<Token>& get_tokens() const { return tokens_; }
};
