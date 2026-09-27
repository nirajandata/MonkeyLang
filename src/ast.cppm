module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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

  using CompoundOp =
      std::variant<Add, Subtract, Multiply, Divide, Remainder, BitwiseAnd,
                   BitwiseOr, BitwiseXor, ShiftLeft, ShiftRight>;

  struct Increment {};

  struct Decrement {};

  using IncDecOp = std::variant<Increment, Decrement>;

  struct Exp;

  struct Var {
    std::string name;
    uint32_t line;
  };

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

  struct Assignment {
    std::unique_ptr<Exp> left;
    std::unique_ptr<Exp> right;
    uint32_t line;
  };

  struct CompoundAssignment {
    CompoundOp op;
    std::unique_ptr<Exp> left;
    std::unique_ptr<Exp> right;
    uint32_t line;
  };

  struct IncDec {
    IncDecOp op;
    std::unique_ptr<Exp> exp;
    bool postfix;
    uint32_t line;
  };

  struct Conditional {
    std::unique_ptr<Exp> condition;
    std::unique_ptr<Exp> then_exp;
    std::unique_ptr<Exp> else_exp;
    uint32_t line;
  };

  struct Exp {
    std::variant<Constant, Var, Unary, Binary, Assignment, CompoundAssignment,
                 IncDec, Conditional> value;
  };

  struct Return {
    Exp value;
    uint32_t line;
  };

  struct Expression {
    Exp value;
    uint32_t line;
  };

  struct Null {};

  struct Statement;

  struct If {
    Exp condition;
    std::unique_ptr<Statement> then_stmt;
    std::unique_ptr<Statement> else_stmt;
    uint32_t line;
  };

  struct Goto {
    std::string label;
    uint32_t line;
  };

  struct Label {
    std::string name;
    std::unique_ptr<Statement> stmt;
    uint32_t line;
  };

  struct Block;

  struct Compound {
    std::unique_ptr<Block> block;
    uint32_t line;
  };

  struct Break {
    std::string label;
    uint32_t line;
  };

  struct Continue {
    std::string label;
    uint32_t line;
  };

  struct While {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> body;
    std::string break_label;
    std::string continue_label;
    uint32_t line;
  };

  struct DoWhile {
    std::unique_ptr<Statement> body;
    std::unique_ptr<Expression> condition;
    std::string break_label;
    std::string continue_label;
    uint32_t line;
  };

  struct Declaration {
    std::string name;
    std::optional<Exp> init;
    uint32_t line;
  };

  struct InitDecl {
    Declaration decl;
  };

  struct InitExp {
    std::optional<Exp> exp;
  };

  using ForInit = std::variant<InitDecl, InitExp>;

  struct For {
    ForInit init;
    std::optional<Expression> condition;
    std::optional<Expression> post;
    std::unique_ptr<Statement> body;
    std::string break_label;
    std::string continue_label;
    uint32_t line;
  };

  struct Statement {
    std::variant<Return, Expression, Null, If, Goto, Label, Compound, Break,
                 Continue, While, DoWhile, For> value;
  };

  using BlockItem = std::variant<Statement, Declaration>;

  struct Block {
    std::vector<BlockItem> items;
  };

  struct Function {
    std::string name;
    Block body;
    uint32_t line;
  };

  struct Program {
    Function function;
  };
}
