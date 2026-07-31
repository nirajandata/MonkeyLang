module;

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

export module ast;

export {
  template <typename... Ts> struct Overload : Ts... {
    using Ts::operator()...;
  };

  struct Constant {
    int32_t value;
    uint32_t line;
  };

  struct Complement {};

  struct Negate {};

  struct Not {};

  using UnaryOp = std::variant<Complement, Negate, Not>;

  struct Add {};

  struct Subtract {};

  struct Multiply {};

  struct Divide {};

  struct Remainder {};

  struct And {};

  struct Or {};

  struct Equal {};

  struct NotEqual {};

  struct LessThan {};

  struct LessOrEqual {};

  struct GreaterThan {};

  struct GreaterOrEqual {};

  struct BitwiseAnd {};

  struct BitwiseOr {};

  struct BitwiseXor {};

  struct ShiftLeft {};

  struct ShiftRight {};

  using BinaryOp =
      std::variant<Add, Subtract, Multiply, Divide, Remainder, And, Or, Equal,
                   NotEqual, LessThan, LessOrEqual, GreaterThan, GreaterOrEqual,
                   BitwiseAnd, BitwiseOr, BitwiseXor, ShiftLeft, ShiftRight>;

  struct Exp;

  struct Unary {
    UnaryOp op;
    std::unique_ptr<Exp> exp;
    uint32_t line;
  };

  struct Binary {
    BinaryOp op;
    std::unique_ptr<Exp> left;
    std::unique_ptr<Exp> right;
    uint32_t line;
  };

  struct Exp {
    std::variant<Constant, Unary, Binary> value;
  };

  struct Return {
    Exp value;
    uint32_t line;
  };

  using Statement = std::variant<Return>;

  struct Function {
    std::string name;
    Statement body;
    uint32_t line;
  };

  struct Program {
    Function function;
  };
}
