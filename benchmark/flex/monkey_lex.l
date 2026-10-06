%option reentrant yylineno noyywrap nounput noinput

%x BLOCK_COMMENT

%{
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "monkey_token_types.h"

#ifndef MONKEY_FLEX_ERROR_FLAG
#define MONKEY_FLEX_ERROR_FLAG monkey_flex_saw_error
#endif

int MONKEY_FLEX_ERROR_FLAG = 0;

static void monkey_flex_mark_error(void) { MONKEY_FLEX_ERROR_FLAG = 1; }

#ifdef MONKEY_FLEX_KEYWORD_RULES
static int monkey_flex_keyword_or_identifier(const char *text, int len) {
  (void)text;
  (void)len;
  return MONKEY_TT_Identifier;
}
#else
#include <immintrin.h>

#define MONKEY_FLEX_MAX_KEYWORD_LEN 8
#define MONKEY_FLEX_VEC_SIZE 8

struct monkey_flex_keyword {
  const char *spelling;
  int type;
};

static const struct monkey_flex_keyword monkey_flex_keywords[] = {
    {"if", MONKEY_TT_If},
    {"else", MONKEY_TT_Else},
    {"int", MONKEY_TT_Int},
    {"void", MONKEY_TT_Void},
    {"return", MONKEY_TT_Return},
    {"goto", MONKEY_TT_Goto},
    {"do", MONKEY_TT_Do},
    {"for", MONKEY_TT_For},
    {"while", MONKEY_TT_While},
    {"break", MONKEY_TT_Break},
    {"continue", MONKEY_TT_Continue},
    {"switch", MONKEY_TT_Switch},
    {"case", MONKEY_TT_Case},
    {"default", MONKEY_TT_Default},
    {"static", MONKEY_TT_Static},
    {"extern", MONKEY_TT_Extern},
};

#define MONKEY_FLEX_NUM_KEYWORDS \
  (sizeof(monkey_flex_keywords) / sizeof(monkey_flex_keywords[0]))

static _Alignas(64) uint64_t monkey_flex_kw_keys[MONKEY_FLEX_NUM_KEYWORDS];
static int monkey_flex_kw_types[MONKEY_FLEX_NUM_KEYWORDS];

static int monkey_flex_kw_ready = 0;

static void monkey_flex_kw_init(void) {
  for (size_t i = 0; i < MONKEY_FLEX_NUM_KEYWORDS; ++i) {
    const char *word = monkey_flex_keywords[i].spelling;
    uint64_t key = 0;
    const size_t len = strlen(word);
    for (size_t j = 0; j < len; ++j)
      key |= (uint64_t)(unsigned char)word[j] << (j * 8);
    monkey_flex_kw_keys[i] = key;
    monkey_flex_kw_types[i] = monkey_flex_keywords[i].type;
  }
  monkey_flex_kw_ready = 1;
}

static int monkey_flex_keyword_or_identifier(const char *text, int len) {
  if (len <= 0 || len > MONKEY_FLEX_MAX_KEYWORD_LEN)
    return MONKEY_TT_Identifier;

  if (!monkey_flex_kw_ready)
    monkey_flex_kw_init();

  uint64_t raw_word;
  memcpy(&raw_word, text, 8);

  const uint64_t word = _bzhi_u64(raw_word, (unsigned)(len * 8));
  const __m512i target = _mm512_set1_epi64((long long)word);
  uint64_t hits = 0;

  for (size_t i = 0; i < MONKEY_FLEX_NUM_KEYWORDS / MONKEY_FLEX_VEC_SIZE; ++i) {
    const __m512i vkeys = _mm512_load_si512(
        (const __m512i *)&monkey_flex_kw_keys[i * MONKEY_FLEX_VEC_SIZE]);
    const __mmask8 mask = _mm512_cmpeq_epi64_mask(target, vkeys);
    hits |= (uint64_t)mask << (i * 8);
  }

  if (hits == 0)
    return MONKEY_TT_Identifier;

  return monkey_flex_kw_types[__builtin_ctzll(hits)];
}
#endif
%}

DIGIT  [0-9]
ALPHA  [A-Za-z_]
IDCHAR [A-Za-z0-9_]
WS     [ \t-\r]

%%

{WS}+                 { }
  [#][^\n]*             { }
"//"[^\n]*            { }

"/*"                  { BEGIN(BLOCK_COMMENT); }
<BLOCK_COMMENT>"*/"   { BEGIN(INITIAL); }
<BLOCK_COMMENT>.|\n   { }
  <BLOCK_COMMENT><<EOF>> { BEGIN(INITIAL); monkey_flex_mark_error(); return 0; }

@MONKEY_FLEX_KEYWORD_RULES@

{ALPHA}{IDCHAR}*      { return monkey_flex_keyword_or_identifier(yytext, yyleng); }
  {DIGIT}+{ALPHA}{IDCHAR}* { monkey_flex_mark_error(); return MONKEY_TT_Error; }
{DIGIT}+              { return MONKEY_TT_Constant; }

"<<="                 { return MONKEY_TT_ShiftLeftAssign; }
">>="                 { return MONKEY_TT_ShiftRightAssign; }
"++"                  { return MONKEY_TT_Increment; }
"--"                  { return MONKEY_TT_Decrement; }
"+="                  { return MONKEY_TT_AddAssign; }
"-="                  { return MONKEY_TT_SubtractAssign; }
"*="                  { return MONKEY_TT_MultiplyAssign; }
"/="                  { return MONKEY_TT_DivideAssign; }
"%="                  { return MONKEY_TT_RemainderAssign; }
"&="                  { return MONKEY_TT_BitwiseAndAssign; }
"|="                  { return MONKEY_TT_BitwiseOrAssign; }
"^="                  { return MONKEY_TT_BitwiseXorAssign; }
"&&"                  { return MONKEY_TT_And; }
"||"                  { return MONKEY_TT_Or; }
"=="                  { return MONKEY_TT_Equal; }
"!="                  { return MONKEY_TT_NotEqual; }
"<="                  { return MONKEY_TT_LessOrEqual; }
">="                  { return MONKEY_TT_GreaterOrEqual; }
"<<"                  { return MONKEY_TT_ShiftLeft; }
">>"                  { return MONKEY_TT_ShiftRight; }
"("                   { return MONKEY_TT_LParen; }
")"                   { return MONKEY_TT_RParen; }
"{"                   { return MONKEY_TT_LBrace; }
"}"                   { return MONKEY_TT_RBrace; }
";"                   { return MONKEY_TT_Semicolon; }
"~"                   { return MONKEY_TT_Complement; }
"-"                   { return MONKEY_TT_Subtract; }
"+"                   { return MONKEY_TT_Add; }
"*"                   { return MONKEY_TT_Multiply; }
"/"                   { return MONKEY_TT_Divide; }
"%"                   { return MONKEY_TT_Remainder; }
"!"                   { return MONKEY_TT_Not; }
"&"                   { return MONKEY_TT_BitwiseAnd; }
"|"                   { return MONKEY_TT_BitwiseOr; }
"="                   { return MONKEY_TT_Assign; }
"<"                   { return MONKEY_TT_LessThan; }
">"                   { return MONKEY_TT_GreaterThan; }
"^"                   { return MONKEY_TT_BitwiseXor; }
"?"                   { return MONKEY_TT_QuestionMark; }
":"                   { return MONKEY_TT_Colon; }
","                   { return MONKEY_TT_Comma; }

  .                     { monkey_flex_mark_error(); return MONKEY_TT_Error; }

%%
