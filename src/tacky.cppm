module;

#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
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

template <typename AST_Node, typename TargetEnum>
consteval std::meta::info reflect_enum_info() {
  std::string_view ast_name = std::meta::identifier_of(^^AST_Node);

  for (std::meta::info e : std::meta::enumerators_of(^^TargetEnum)) {
    if (std::meta::identifier_of(e) == ast_name) {
      return e;
    }
  }

  throw "AST Node does not match any TargetEnum member!";
}

template <typename AST_Node, typename TargetEnum>
consteval TargetEnum reflect_to_enum() {
  return [:reflect_enum_info<AST_Node, TargetEnum>():];
}

class TackyEmitter {
  int temp_counter = 0;
  int label_counter = 0;

  std::string make_temporary() {
    return "tmp." + std::to_string(temp_counter++);
  }

  std::string make_label(std::string_view prefix) {
    return std::string(prefix) + "." + std::to_string(label_counter++);
  }

  TackyVal emit_and(const Binary &b,
                    std::vector<TackyInstruction> &instructions) {
    TackyVal dst = TackyVar{make_temporary()};
    auto false_label = make_label("and_false");
    auto end_label = make_label("and_end");

    auto left = emit_val(*b.left, instructions);
    instructions.push_back(TackyJumpIfZero{left, false_label});

    auto right = emit_val(*b.right, instructions);
    instructions.push_back(TackyJumpIfZero{right, false_label});

    instructions.push_back(TackyCopy{TackyConstant{1}, dst});
    instructions.push_back(TackyJump{end_label});
    instructions.push_back(TackyLabel{false_label});
    instructions.push_back(TackyCopy{TackyConstant{0}, dst});
    instructions.push_back(TackyLabel{end_label});

    return dst;
  }

  TackyVal emit_or(const Binary &b,
                   std::vector<TackyInstruction> &instructions) {
    TackyVal dst = TackyVar{make_temporary()};
    auto true_label = make_label("or_true");
    auto end_label = make_label("or_end");

    auto left = emit_val(*b.left, instructions);
    instructions.push_back(TackyJumpIfNotZero{left, true_label});

    auto right = emit_val(*b.right, instructions);
    instructions.push_back(TackyJumpIfNotZero{right, true_label});

    instructions.push_back(TackyCopy{TackyConstant{0}, dst});
    instructions.push_back(TackyJump{end_label});
    instructions.push_back(TackyLabel{true_label});
    instructions.push_back(TackyCopy{TackyConstant{1}, dst});
    instructions.push_back(TackyLabel{end_label});

    return dst;
  }

  TackyVal emit_val(const Exp &exp,
                    std::vector<TackyInstruction> &instructions) {
    return std::visit(
        Overload{[](const Constant &c) -> TackyVal {
                   return TackyConstant{c.value};
                 },

                 [&](const Unary &u) -> TackyVal {
                   return std::visit(
                       [&](const auto &op) -> TackyVal {
                         using OpType = std::decay_t<decltype(op)>;

                         auto src = emit_val(*u.exp, instructions);
                         TackyVal dst = TackyVar{make_temporary()};
                         instructions.push_back(
                             TackyUnary{reflect_to_enum<OpType, TackyUnaryOp>(),
                                        std::move(src), dst});
                         return dst;
                       },
                       u.op);
                 },

                 [&](const Binary &b) -> TackyVal {
                   return std::visit(
                       [&](const auto &op) -> TackyVal {
                         using OpType = std::decay_t<decltype(op)>;

                         if constexpr (std::is_same_v<OpType, And>) {
                           return emit_and(b, instructions);
                         } else if constexpr (std::is_same_v<OpType, Or>) {
                           return emit_or(b, instructions);
                         } else {
                           auto left = emit_val(*b.left, instructions);
                           auto right = emit_val(*b.right, instructions);
                           TackyVal dst = TackyVar{make_temporary()};

                           instructions.push_back(TackyBinary{
                               reflect_to_enum<OpType, TackyBinaryOp>(),
                               std::move(left), std::move(right), dst});
                           return dst;
                         }
                       },
                       b.op);
                 }},
        exp.value);
  }

  TackyFunction emit_function(const Function &func) {
    std::vector<TackyInstruction> instructions;
    std::visit(Overload{[&](const Return &r) {
                 instructions.push_back(
                     TackyReturn{emit_val(r.value, instructions)});
               }},
               func.body);

    return {std::string(func.name), std::move(instructions)};
  }

public:
  TackyProgram emit_program(const Program &program) {
    return {emit_function(program.function)};
  }
};

export TackyProgram emit_tacky(const Program &program) {
  TackyEmitter emitter;
  return emitter.emit_program(program);
}