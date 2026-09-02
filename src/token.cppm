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
  Divide,
  Complement,
  Subtract,
  Decrement,
  Add,
  Multiply,
  Remainder,
  Not,
  And,
  Or,
  Assign,
  Equal,
  NotEqual,
  LessThan,
  GreaterThan,
  LessOrEqual,
  GreaterOrEqual,
  BitwiseAnd,
  BitwiseOr,
  BitwiseXor,
  ShiftLeft,
  ShiftRight,
  Error,
  Eof
};

export struct Token {
  TokenType type;
  std::string_view text;
  uint32_t line;
};