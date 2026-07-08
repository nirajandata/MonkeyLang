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

    inline uint64_t tail_mask() const
    {
        uint64_t remaining = static_cast<uint64_t>(limit_ - cursor_);
        return remaining >= 64 ? ~0ULL : ((1ULL << remaining) - 1);
    }

    void skip_whitespace()
    {
        while (cursor_ < limit_)
        {
            __m512i chars = _mm512_loadu_si512(cursor_);
            uint64_t mask = tail_mask();

            uint64_t ws = ascii::is_space_512(chars) & mask;
            uint64_t nl = static_cast<uint64_t>(_mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('\n'))) & mask;
            uint64_t not_ws = (~ws) & mask;

            if (not_ws == 0)
            {
                current_line_ += std::popcount(nl);
                cursor_ += (mask == ~0ULL) ? 64 : (limit_ - cursor_);
            }
            else
            {
                int offset = std::countr_zero(not_ws);
                uint64_t nl_skipped = nl & ((1ULL << offset) - 1);
                current_line_ += std::popcount(nl_skipped);
                cursor_ += offset;
                return;
            }
        }
    }

    void skip_line_comment()
    {
        while (cursor_ < limit_)
        {
            __m512i chars = _mm512_loadu_si512(cursor_);
            uint64_t mask = tail_mask();
            uint64_t nl = static_cast<uint64_t>(_mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('\n'))) & mask;

            if (nl == 0)
            {
                cursor_ += (mask == ~0ULL) ? 64 : (limit_ - cursor_);
            }
            else
            {
                int offset = std::countr_zero(nl);
                cursor_ += offset;
                return;
            }
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

        while (cursor_ < limit_)
        {
            __m512i chars = _mm512_loadu_si512(cursor_);
            uint64_t mask = tail_mask();

            uint64_t is_under = static_cast<uint64_t>(_mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8('_')));
            uint64_t valid = (ascii::is_alnum_512(chars) | is_under) & mask;

            if (valid == mask)
            {
                cursor_ += (mask == ~0ULL) ? 64 : (limit_ - cursor_);
            }
            else
            {
                int offset = std::countr_zero(~valid);
                cursor_ += offset;
                break;
            }
        }

        std::string_view text(start, cursor_ - start);
        TokenType type = TokenType::Identifier;

        switch (text.size())
        {
        case 3:
            if (text == "int") type = TokenType::Int;
            break;
        case 4:
            if (text == "void") type = TokenType::Void;
            break;
        case 6:
            if (text == "return") type = TokenType::Return;
            break;
        default:
            break;
        }

        return {type, text, current_line_};
    }

    Token read_constant()
    {
        const char* start = cursor_;

        while (cursor_ < limit_)
        {
            __m512i chars = _mm512_loadu_si512(cursor_);
            uint64_t mask = tail_mask();

            uint64_t is_digit = ascii::is_digit_512(chars) & mask;

            if (is_digit == mask)
            {
                cursor_ += (mask == ~0ULL) ? 64 : (limit_ - cursor_);
            }
            else
            {
                int offset = std::countr_zero(~is_digit);
                cursor_ += offset;
                break;
            }
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
            auto size = static_cast<size_t>(file.tellg());
            source_.resize(size + 64, '\0');
            file.seekg(0);
            file.read(source_.data(), static_cast<std::streamsize>(size));
            cursor_ = source_.data();
            limit_ = source_.data() + size;
        }
        else
        {
            cursor_ = source_.data();
            limit_ = source_.data();
        }
    }

    void lex()
    {
        tokens_.clear();
        tokens_.reserve(source_.size() / 3 + 16);

        had_error_ = false;
        current_line_ = 1;
        cursor_ = source_.data();

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