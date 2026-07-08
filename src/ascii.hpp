#pragma once
#include <immintrin.h>
#include <cstdint>

namespace ascii {

    constexpr bool is_alpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    constexpr bool is_digit(char c) {
        return c >= '0' && c <= '9';
    }

    constexpr bool is_alnum(char c) {
        return is_alpha(c) || is_digit(c);
    }

    constexpr bool is_space(char c) {
        return c == ' ' || (c >= '\t' && c <= '\r');
    }

    inline __mmask64 in_range_512(__m512i chars, uint8_t lo, uint8_t hi)
    {
        __m512i shifted = _mm512_sub_epi8(chars, _mm512_set1_epi8(static_cast<char>(lo)));
        return _mm512_cmple_epu8_mask(shifted, _mm512_set1_epi8(static_cast<char>(hi - lo)));
    }

    inline __mmask64 is_alpha_512(__m512i chars)
    {
        __m512i lowered = _mm512_or_si512(chars, _mm512_set1_epi8(0x20));
        return in_range_512(lowered, 'a', 'z');
    }

    inline __mmask64 is_digit_512(__m512i chars)
    {
        return in_range_512(chars, '0', '9');
    }

    inline __mmask64 is_alnum_512(__m512i chars)
    {
        return is_alpha_512(chars) | is_digit_512(chars);
    }

    inline __mmask64 is_space_512(__m512i chars)
    {
        __mmask64 space = _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8(' '));
        __mmask64 range = in_range_512(chars, '\t', '\r');
        return space | range;
    }

}