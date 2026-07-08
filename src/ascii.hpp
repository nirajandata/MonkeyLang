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

    inline __mmask64 is_alpha_512(__m512i chars) {
        __mmask64 lower = _mm512_cmpge_epu8_mask(chars, _mm512_set1_epi8('a')) &
                          _mm512_cmple_epu8_mask(chars, _mm512_set1_epi8('z'));
        __mmask64 upper = _mm512_cmpge_epu8_mask(chars, _mm512_set1_epi8('A')) &
                          _mm512_cmple_epu8_mask(chars, _mm512_set1_epi8('Z'));
        return lower | upper;
    }

    inline __mmask64 is_digit_512(__m512i chars) {
        return _mm512_cmpge_epu8_mask(chars, _mm512_set1_epi8('0')) &
               _mm512_cmple_epu8_mask(chars, _mm512_set1_epi8('9'));
    }

    inline __mmask64 is_alnum_512(__m512i chars) {
        return is_alpha_512(chars) | is_digit_512(chars);
    }

    inline __mmask64 is_space_512(__m512i chars) {
        __mmask64 space = _mm512_cmpeq_epi8_mask(chars, _mm512_set1_epi8(' '));
        __mmask64 range = _mm512_cmpge_epu8_mask(chars, _mm512_set1_epi8('\t')) &
                          _mm512_cmple_epu8_mask(chars, _mm512_set1_epi8('\r'));
        return space | range;
    }
}