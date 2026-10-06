#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "monkey_token_types.h"
#include "monkey_re2c_scanner.h"

#define YYFILL(n) return 0;

int monkey_re2c_saw_error;

#define MONKEY_RE2C_MAX_KEYWORD_LEN 8
#define MONKEY_RE2C_NUM_KEYWORDS 16

static const char *const monkey_re2c_keywords[MONKEY_RE2C_NUM_KEYWORDS] = {
    "if",     "else",   "int",    "void",  "return", "goto",
    "do",     "for",    "while",  "break", "continue", "switch",
    "case",   "default", "static", "extern",
};

static const int monkey_re2c_keyword_types[MONKEY_RE2C_NUM_KEYWORDS] = {
    MONKEY_TT_If,     MONKEY_TT_Else,   MONKEY_TT_Int,  MONKEY_TT_Void,
    MONKEY_TT_Return, MONKEY_TT_Goto,   MONKEY_TT_Do,   MONKEY_TT_For,
    MONKEY_TT_While,  MONKEY_TT_Break,  MONKEY_TT_Continue, MONKEY_TT_Switch,
    MONKEY_TT_Case,   MONKEY_TT_Default, MONKEY_TT_Static, MONKEY_TT_Extern,
};

static int monkey_re2c_keyword_or_identifier(const char *text, size_t len) {
  if (len == 0 || len > MONKEY_RE2C_MAX_KEYWORD_LEN)
    return MONKEY_TT_Identifier;

  for (size_t i = 0; i < MONKEY_RE2C_NUM_KEYWORDS; ++i) {
    const char *word = monkey_re2c_keywords[i];
    if (strlen(word) != len)
      continue;
    if (memcmp(word, text, len) == 0)
      return monkey_re2c_keyword_types[i];
  }

  return MONKEY_TT_Identifier;
}

static int monkey_re2c_count_nl(const char *p, size_t n) {
  int count = 0;
  for (size_t i = 0; i < n; ++i)
    count += (p[i] == '\n');
  return count;
}

int monkey_re2c_lex(void *scanner) {
  struct monkey_re2c_scanner *sc = (struct monkey_re2c_scanner *)scanner;
  const char *YYCURSOR = sc->cursor;
  const char *YYLIMIT = sc->limit;
  const char *YYMARKER = 0;
  const char *tok = YYCURSOR;
  int type = 0;

  for (;;) {
  loop:
    tok = YYCURSOR;

    /*!re2c
      re2c:define:YYCTYPE = "unsigned char";
      re2c:define:YYCURSOR = YYCURSOR;
      re2c:define:YYLIMIT = YYLIMIT;
      re2c:define:YYMARKER = YYMARKER;
      re2c:yyfill:enable = 1;
      re2c:flags:tags = 0;

      WS     = [ \t-\r];
      LF     = "\n";
      DIGIT  = [0-9];
      ALPHA  = [A-Za-z_];
      IDCHAR = [A-Za-z0-9_];

      WS+              { sc->lineno += monkey_re2c_count_nl(tok, (size_t)(YYCURSOR - tok)); goto loop; }
      "#" [^\n\x00]*       { goto loop; }
      "//" [^\n\x00]*      { goto loop; }
      "/*" ([^\x00*] | "*"+ [^\x00*/])* "*"+ "/"  { sc->lineno += monkey_re2c_count_nl(tok, (size_t)(YYCURSOR - tok)); goto loop; }
      "/*" ([^\x00*] | "*"+ [^\x00*/])*      { sc->lineno += monkey_re2c_count_nl(tok, (size_t)(YYCURSOR - tok)); monkey_re2c_saw_error = 1; return 0; }

      ALPHA IDCHAR*    { type = monkey_re2c_keyword_or_identifier(tok, (size_t)(YYCURSOR - tok)); break; }
      DIGIT+ ALPHA IDCHAR* { monkey_re2c_saw_error = 1; type = MONKEY_TT_Error; break; }
      DIGIT+           { type = MONKEY_TT_Constant; break; }

      "<<="            { type = MONKEY_TT_ShiftLeftAssign; break; }
      ">>="            { type = MONKEY_TT_ShiftRightAssign; break; }
      "++"             { type = MONKEY_TT_Increment; break; }
      "--"             { type = MONKEY_TT_Decrement; break; }
      "+="             { type = MONKEY_TT_AddAssign; break; }
      "-="             { type = MONKEY_TT_SubtractAssign; break; }
      "*="             { type = MONKEY_TT_MultiplyAssign; break; }
      "/="             { type = MONKEY_TT_DivideAssign; break; }
      "%="             { type = MONKEY_TT_RemainderAssign; break; }
      "&="             { type = MONKEY_TT_BitwiseAndAssign; break; }
      "|="             { type = MONKEY_TT_BitwiseOrAssign; break; }
      "^="             { type = MONKEY_TT_BitwiseXorAssign; break; }
      "&&"             { type = MONKEY_TT_And; break; }
      "||"             { type = MONKEY_TT_Or; break; }
      "=="             { type = MONKEY_TT_Equal; break; }
      "!="             { type = MONKEY_TT_NotEqual; break; }
      "<="             { type = MONKEY_TT_LessOrEqual; break; }
      ">="             { type = MONKEY_TT_GreaterOrEqual; break; }
      "<<"             { type = MONKEY_TT_ShiftLeft; break; }
      ">>"             { type = MONKEY_TT_ShiftRight; break; }
      "("              { type = MONKEY_TT_LParen; break; }
      ")"              { type = MONKEY_TT_RParen; break; }
      "{"              { type = MONKEY_TT_LBrace; break; }
      "}"              { type = MONKEY_TT_RBrace; break; }
      ";"              { type = MONKEY_TT_Semicolon; break; }
      "~"              { type = MONKEY_TT_Complement; break; }
      "-"              { type = MONKEY_TT_Subtract; break; }
      "+"              { type = MONKEY_TT_Add; break; }
      "*"              { type = MONKEY_TT_Multiply; break; }
      "/"              { type = MONKEY_TT_Divide; break; }
      "%"              { type = MONKEY_TT_Remainder; break; }
      "!"              { type = MONKEY_TT_Not; break; }
      "&"              { type = MONKEY_TT_BitwiseAnd; break; }
      "|"              { type = MONKEY_TT_BitwiseOr; break; }
      "="              { type = MONKEY_TT_Assign; break; }
      "<"              { type = MONKEY_TT_LessThan; break; }
      ">"              { type = MONKEY_TT_GreaterThan; break; }
      "^"              { type = MONKEY_TT_BitwiseXor; break; }
      "?"              { type = MONKEY_TT_QuestionMark; break; }
      ":"              { type = MONKEY_TT_Colon; break; }
      ","              { type = MONKEY_TT_Comma; break; }

      "\x00"           { if (YYCURSOR - 1 == sc->end) return 0; monkey_re2c_saw_error = 1; type = MONKEY_TT_Error; break; }

      [^\x00]          { monkey_re2c_saw_error = 1; type = MONKEY_TT_Error; break; }
  */

    break;
  }

  sc->token = tok;
  sc->len = (size_t)(YYCURSOR - tok);
  sc->cursor = YYCURSOR;
  return type;
}