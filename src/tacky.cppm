module;

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module tacky;

import ast;

export {
  struct TackyConstant {
    int32_t value;
  };
  struct TackyVar {
    std::string name;
  };
  using TackyVal = std::variant<TackyConstant, TackyVar>;

  enum class TackyUnaryOp : uint8_t { Complement, Negate, Not };
  enum class TackyBinaryOp : uint8_t {
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    Equal,
    NotEqual,
    LessThan,
    LessOrEqual,
    GreaterThan,
    GreaterOrEqual,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    ShiftLeft,
    ShiftRight
  };

  struct TackyReturn {
    TackyVal val;
  };
  struct TackyUnary {
    TackyUnaryOp op;
    TackyVal src;
    TackyVal dst;
  };
  struct TackyBinary {
    TackyBinaryOp op;
    TackyVal src1;
    TackyVal src2;
    TackyVal dst;
  };
  struct TackyCopy {
    TackyVal src;
    TackyVal dst;
  };
  struct TackyJump {
    std::string target;
  };
  struct TackyJumpIfZero {
    TackyVal condition;
    std::string target;
  };
  struct TackyJumpIfNotZero {
    TackyVal condition;
    std::string target;
  };
  struct TackyLabel {
    std::string name;
  };

  using TackyInstruction =
      std::variant<TackyReturn, TackyUnary, TackyBinary, TackyCopy, TackyJump,
                   TackyJumpIfZero, TackyJumpIfNotZero, TackyLabel>;

  struct TackyFunction {
    std::string name;
    std::vector<TackyInstruction> instructions;
  };
  struct TackyProgram {
    TackyFunction function;
  };
}

static int temp_counter = 0;
static int label_counter = 0;

static std::string make_temporary() {
  return "tmp." + std::to_string(temp_counter++);
}

static std::string make_label(std::string_view prefix) {
  return std::string(prefix) + "." + std::to_string(label_counter++);
}

static TackyUnaryOp convert_ast_unop(const UnaryOp &op) {
  if (std::holds_alternative<Complement>(op))
    return TackyUnaryOp::Complement;
  if (std::holds_alternative<Negate>(op))
    return TackyUnaryOp::Negate;
  return TackyUnaryOp::Not;
}

static TackyBinaryOp convert_ast_binop(const BinaryOp &op) {
  if (std::holds_alternative<Add>(op))
    return TackyBinaryOp::Add;
  if (std::holds_alternative<Subtract>(op))
    return TackyBinaryOp::Subtract;
  if (std::holds_alternative<Multiply>(op))
    return TackyBinaryOp::Multiply;
  if (std::holds_alternative<Divide>(op))
    return TackyBinaryOp::Divide;
  if (std::holds_alternative<Remainder>(op))
    return TackyBinaryOp::Remainder;
  if (std::holds_alternative<Equal>(op))
    return TackyBinaryOp::Equal;
  if (std::holds_alternative<NotEqual>(op))
    return TackyBinaryOp::NotEqual;
  if (std::holds_alternative<LessThan>(op))
    return TackyBinaryOp::LessThan;
  if (std::holds_alternative<LessOrEqual>(op))
    return TackyBinaryOp::LessOrEqual;
  if (std::holds_alternative<GreaterThan>(op))
    return TackyBinaryOp::GreaterThan;
  if (std::holds_alternative<BitwiseAnd>(op))
    return TackyBinaryOp::BitwiseAnd;
  if (std::holds_alternative<BitwiseOr>(op))
    return TackyBinaryOp::BitwiseOr;
  if (std::holds_alternative<BitwiseXor>(op))
    return TackyBinaryOp::BitwiseXor;
  if (std::holds_alternative<ShiftLeft>(op))
    return TackyBinaryOp::ShiftLeft;
  if (std::holds_alternative<ShiftRight>(op))
    return TackyBinaryOp::ShiftRight;
  return TackyBinaryOp::GreaterOrEqual;
}

static TackyVal emit_tacky_val(const Exp &exp,
                               std::vector<TackyInstruction> &instructions) {
  return std::visit(
      Overload{
          [](const Constant &c) -> TackyVal { return TackyConstant{c.value}; },
          [&](const Unary &u) -> TackyVal {
            auto src = emit_tacky_val(*u.exp, instructions);
            TackyVal dst = TackyVar{make_temporary()};
            instructions.push_back(
                TackyUnary{convert_ast_unop(u.op), src, dst});
            return dst;
          },
          [&](const Binary &b) -> TackyVal {
            bool is_and = std::holds_alternative<And>(b.op);
            bool is_or = std::holds_alternative<Or>(b.op);

            if (!is_and && !is_or) {
              auto left = emit_tacky_val(*b.left, instructions);
              auto right = emit_tacky_val(*b.right, instructions);
              TackyVal dst = TackyVar{make_temporary()};
              instructions.push_back(
                  TackyBinary{convert_ast_binop(b.op), left, right, dst});
              return dst;
            }

            TackyVal dst = TackyVar{make_temporary()};
            auto left = emit_tacky_val(*b.left, instructions);
            auto false_label = make_label(is_and ? "and_false" : "or_false");
            auto end_label = make_label(is_and ? "and_end" : "or_end");

            if (is_and) {
              instructions.push_back(TackyJumpIfZero{left, false_label});
              auto right = emit_tacky_val(*b.right, instructions);
              instructions.push_back(TackyJumpIfZero{right, false_label});
              instructions.push_back(TackyCopy{TackyConstant{1}, dst});
              instructions.push_back(TackyJump{end_label});
              instructions.push_back(TackyLabel{false_label});
              instructions.push_back(TackyCopy{TackyConstant{0}, dst});
            } else {
              instructions.push_back(TackyJumpIfNotZero{left, false_label});
              auto right = emit_tacky_val(*b.right, instructions);
              instructions.push_back(TackyJumpIfNotZero{right, false_label});
              instructions.push_back(TackyCopy{TackyConstant{0}, dst});
              instructions.push_back(TackyJump{end_label});
              instructions.push_back(TackyLabel{false_label});
              instructions.push_back(TackyCopy{TackyConstant{1}, dst});
            }
            instructions.push_back(TackyLabel{end_label});
            return dst;
          },
      },
      exp.value);
}

static TackyFunction emit_tacky_function(const Function &func) {
  std::vector<TackyInstruction> instructions;
  std::visit(
      Overload{
          [&](const Return &r) {
            instructions.push_back(
                TackyReturn{emit_tacky_val(r.value, instructions)});
          },
      },
      func.body);
  return {std::string(func.name), std::move(instructions)};
}

export TackyProgram emit_tacky(const Program &program) {
  temp_counter = 0;
  label_counter = 0;
  return {emit_tacky_function(program.function)};
}
