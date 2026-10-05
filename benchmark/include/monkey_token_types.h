#pragma once

#define MONKEY_TOKEN_LIST(X)                                                \
  X(Identifier, 1)                                                          \
  X(Constant, 2)                                                            \
  X(Int, 3)                                                                 \
  X(Void, 4)                                                                \
  X(Return, 5)                                                              \
  X(LParen, 6)                                                              \
  X(RParen, 7)                                                              \
  X(LBrace, 8)                                                              \
  X(RBrace, 9)                                                              \
  X(Semicolon, 10)                                                          \
  X(Divide, 11)                                                             \
  X(Complement, 12)                                                         \
  X(Subtract, 13)                                                          \
  X(Decrement, 14)                                                          \
  X(Add, 15)                                                                \
  X(Multiply, 16)                                                           \
  X(Remainder, 17)                                                          \
  X(Not, 18)                                                                \
  X(And, 19)                                                                \
  X(Or, 20)                                                                 \
  X(Assign, 21)                                                             \
  X(Equal, 22)                                                              \
  X(NotEqual, 23)                                                           \
  X(LessThan, 24)                                                           \
  X(GreaterThan, 25)                                                        \
  X(LessOrEqual, 26)                                                        \
  X(GreaterOrEqual, 27)                                                     \
  X(BitwiseAnd, 28)                                                         \
  X(BitwiseOr, 29)                                                          \
  X(BitwiseXor, 30)                                                         \
  X(ShiftLeft, 31)                                                          \
  X(ShiftRight, 32)                                                         \
  X(Error, 33)                                                              \
  X(Eof, 34)                                                                \
  X(Increment, 35)                                                          \
  X(AddAssign, 36)                                                          \
  X(SubtractAssign, 37)                                                     \
  X(MultiplyAssign, 38)                                                     \
  X(DivideAssign, 39)                                                       \
  X(RemainderAssign, 40)                                                    \
  X(BitwiseAndAssign, 41)                                                   \
  X(BitwiseOrAssign, 42)                                                    \
  X(BitwiseXorAssign, 43)                                                   \
  X(ShiftLeftAssign, 44)                                                    \
  X(ShiftRightAssign, 45)                                                   \
  X(If, 46)                                                                 \
  X(Else, 47)                                                               \
  X(QuestionMark, 48)                                                       \
  X(Colon, 49)                                                              \
  X(Goto, 50)                                                               \
  X(Do, 51)                                                                 \
  X(While, 52)                                                              \
  X(For, 53)                                                                \
  X(Break, 54)                                                              \
  X(Continue, 55)                                                           \
  X(Comma, 56)                                                              \
  X(Switch, 57)                                                             \
  X(Case, 58)                                                               \
  X(Default, 59)                                                            \
  X(Static, 60)                                                             \
  X(Extern, 61)

#ifdef __cplusplus
extern "C" {
#endif

#define MONKEY_TOKEN_ENUM_ENTRY(name, value) MONKEY_TT_##name = (value),

enum monkey_token_type {
  MONKEY_TOKEN_LIST(MONKEY_TOKEN_ENUM_ENTRY)
  MONKEY_TT_COUNT
};

#ifdef __cplusplus
}
#endif

