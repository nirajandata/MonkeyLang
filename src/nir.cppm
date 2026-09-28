module;

#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module nir;

import ast;
import semantic;

export {
  struct NirConstant {
    int32_t value;
  };
  struct NirVar {
    std::string name;
  };
  using NirVal = std::variant<NirConstant, NirVar>;

  enum class NirUnaryOp : uint8_t { Complement, Negate, Not };
  enum class NirBinaryOp : uint8_t {
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

  struct NirReturn {
    NirVal val;
  };
  struct NirUnary {
    NirUnaryOp op;
    NirVal src;
    NirVal dst;
  };
  struct NirBinary {
    NirBinaryOp op;
    NirVal src1;
    NirVal src2;
    NirVal dst;
  };
  struct NirCopy {
    NirVal src;
    NirVal dst;
  };
  struct NirJump {
    std::string target;
  };
  struct NirJumpIfZero {
    NirVal condition;
    std::string target;
  };
  struct NirJumpIfNotZero {
    NirVal condition;
    std::string target;
  };
  struct NirJumpIfNotEqual {
    NirVal value1;
    NirVal value2;
    std::string target;
  };
  struct NirLabel {
    std::string name;
  };

  using NirInstruction =
      std::variant<NirReturn, NirUnary, NirBinary, NirCopy, NirJump,
                   NirJumpIfZero, NirJumpIfNotZero, NirJumpIfNotEqual,
                   NirLabel>;

  struct NirFunction {
    std::string name;
    std::vector<NirInstruction> instructions;
  };
  struct NirProgram {
    NirFunction function;
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

class NirEmitter {
  std::string make_temporary() {
    return "tmp." + std::to_string(next_name_id());
  }

  std::string make_label(std::string_view prefix) {
    return std::string(prefix) + "." + std::to_string(next_name_id());
  }

  NirVal emit_and(const Binary &b,
                    std::vector<NirInstruction> &instructions) {
    NirVal dst = NirVar{make_temporary()};
    auto false_label = make_label("and_false");
    auto end_label = make_label("and_end");

    auto left = emit_val(*b.left, instructions);
    instructions.push_back(NirJumpIfZero{left, false_label});

    auto right = emit_val(*b.right, instructions);
    instructions.push_back(NirJumpIfZero{right, false_label});

    instructions.push_back(NirCopy{NirConstant{1}, dst});
    instructions.push_back(NirJump{end_label});
    instructions.push_back(NirLabel{false_label});
    instructions.push_back(NirCopy{NirConstant{0}, dst});
    instructions.push_back(NirLabel{end_label});

    return dst;
  }

  NirVal emit_or(const Binary &b,
                   std::vector<NirInstruction> &instructions) {
    NirVal dst = NirVar{make_temporary()};
    auto true_label = make_label("or_true");
    auto end_label = make_label("or_end");

    auto left = emit_val(*b.left, instructions);
    instructions.push_back(NirJumpIfNotZero{left, true_label});

    auto right = emit_val(*b.right, instructions);
    instructions.push_back(NirJumpIfNotZero{right, true_label});

    instructions.push_back(NirCopy{NirConstant{0}, dst});
    instructions.push_back(NirJump{end_label});
    instructions.push_back(NirLabel{true_label});
    instructions.push_back(NirCopy{NirConstant{1}, dst});
    instructions.push_back(NirLabel{end_label});

    return dst;
  }

  NirVal emit_conditional(const Conditional &c,
                          std::vector<NirInstruction> &instructions) {
    NirVal dst = NirVar{make_temporary()};
    auto then_label = make_label("then");
    auto else_label = make_label("else");
    auto end_label = make_label("end");

    auto condition = emit_val(*c.condition, instructions);
    instructions.push_back(NirJumpIfZero{std::move(condition), else_label});

    auto then_val = emit_val(*c.then_exp, instructions);
    instructions.push_back(NirCopy{std::move(then_val), dst});
    instructions.push_back(NirJump{end_label});
    instructions.push_back(NirLabel{else_label});

    auto else_val = emit_val(*c.else_exp, instructions);
    instructions.push_back(NirCopy{std::move(else_val), dst});

    instructions.push_back(NirLabel{then_label});
    instructions.push_back(NirLabel{end_label});
    return dst;
  }

  NirVal emit_val(const Exp &exp,
                    std::vector<NirInstruction> &instructions) {
    return std::visit(
        Overload{[](const Constant &c) -> NirVal {
                   return NirConstant{c.value};
                 },

                 [](const Var &v) -> NirVal {
                   return NirVar{v.name};
                 },

                 [&](const Assignment &a) -> NirVal {
                   auto right = emit_val(*a.right, instructions);
                   auto left = emit_val(*a.left, instructions);
                   instructions.push_back(NirCopy{std::move(right), left});
                   return left;
                 },

                 [&](const CompoundAssignment &a) -> NirVal {
                   auto left = emit_val(*a.left, instructions);
                   auto old = NirVal{NirVar{make_temporary()}};
                   instructions.push_back(NirCopy{left, old});
                   auto right = emit_val(*a.right, instructions);
                   auto dst = NirVal{NirVar{make_temporary()}};

                   auto op = std::visit(
                       [&](const auto &value) {
                         using OpType = std::decay_t<decltype(value)>;
                         return reflect_to_enum<OpType, NirBinaryOp>();
                       },
                       a.op);
                   instructions.push_back(
                       NirBinary{op, old, std::move(right), dst});
                   instructions.push_back(NirCopy{dst, left});
                   return left;
                 },

                 [&](const IncDec &e) -> NirVal {
                   auto value = emit_val(*e.exp, instructions);
                   auto next = NirVal{NirVar{make_temporary()}};
                   auto old = e.postfix
                                  ? NirVal{NirVar{make_temporary()}}
                                  : NirVal{};
                   if (e.postfix) instructions.push_back(NirCopy{value, old});

                   auto op = std::visit(
                       [&](const auto &value) {
                         using OpType = std::decay_t<decltype(value)>;
                         if constexpr (std::is_same_v<OpType, Increment>) {
                           return NirBinaryOp::Add;
                         } else {
                           return NirBinaryOp::Subtract;
                         }
                       },
                       e.op);
                   instructions.push_back(
                       NirBinary{op, value, NirConstant{1}, next});
                   instructions.push_back(NirCopy{next, value});
                   return e.postfix ? old : value;
                 },

                 [&](const Unary &u) -> NirVal {
                   return std::visit(
                       [&](const auto &op) -> NirVal {
                         using OpType = std::decay_t<decltype(op)>;

                         auto src = emit_val(*u.exp, instructions);
                         NirVal dst = NirVar{make_temporary()};
                         instructions.push_back(
                             NirUnary{reflect_to_enum<OpType, NirUnaryOp>(),
                                        std::move(src), dst});
                         return dst;
                       },
                       u.op);
                 },

                 [&](const Binary &b) -> NirVal {
                   return std::visit(
                       [&](const auto &op) -> NirVal {
                         using OpType = std::decay_t<decltype(op)>;

                         if constexpr (std::is_same_v<OpType, And>) {
                           return emit_and(b, instructions);
                         } else if constexpr (std::is_same_v<OpType, Or>) {
                           return emit_or(b, instructions);
                         } else {
                           auto left = emit_val(*b.left, instructions);
                           auto right = emit_val(*b.right, instructions);
                           NirVal dst = NirVar{make_temporary()};

                           instructions.push_back(NirBinary{
                               reflect_to_enum<OpType, NirBinaryOp>(),
                               std::move(left), std::move(right), dst});
                           return dst;
                         }
                       },
                       b.op);
                  },

                  [&](const Conditional &c) -> NirVal {
                    return emit_conditional(c, instructions);
                  }},
        exp.value);
  }

  void emit_statement(const Statement &stmt,
                      std::vector<NirInstruction> &instructions) {
    std::visit(
        Overload{
            [&](const Return &r) {
              instructions.push_back(
                  NirReturn{emit_val(r.value, instructions)});
            },
            [&](const Expression &e) { emit_val(e.value, instructions); },
            [&](const Null &) {},
            [&](const If &i) {
              auto then_label = make_label("then");
              auto else_label = make_label("else");
              auto end_label = make_label("end");

              auto condition = emit_val(i.condition, instructions);
              instructions.push_back(
                  NirJumpIfZero{std::move(condition), else_label});

              emit_statement(*i.then_stmt, instructions);
              instructions.push_back(NirJump{end_label});
              instructions.push_back(NirLabel{else_label});

              if (i.else_stmt) emit_statement(*i.else_stmt, instructions);

              instructions.push_back(NirLabel{end_label});
            },
            [&](const Goto &g) {
              instructions.push_back(NirJump{g.label});
            },
            [&](const Label &l) {
              instructions.push_back(NirLabel{l.name});
              emit_statement(*l.stmt, instructions);
            },
            [&](const Compound &c) { emit_block(*c.block, instructions); },
            [&](const Break &b) { instructions.push_back(NirJump{b.label}); },
            [&](const Continue &c) { instructions.push_back(NirJump{c.label}); },
            [&](const Case &c) {
              instructions.push_back(NirLabel{c.label});
              emit_statement(*c.stmt, instructions);
            },
            // The label for the default statement is emitted here, in the
            // place where the default statement appears in the body. The
            // switch statement below jumps to it when the controlling
            // expression matches none of the case values.
            [&](const Default &d) {
              instructions.push_back(NirLabel{d.label});
              emit_statement(*d.stmt, instructions);
            },
            [&](const Switch &s) {
              // If the controlling expression matches none of the cases, we
              // jump to the default statement, or past the end of the switch
              // statement if it doesn't have one.
              const auto &default_target =
                  s.default_case ? s.default_case->label : s.break_label;

              auto condition = emit_val(s.condition, instructions);

              for (size_t i = 0; i < s.cases.size(); ++i) {
                const bool last = (i + 1 == s.cases.size());
                auto next_label =
                    last ? default_target : make_label("case_next");

                auto case_value = emit_val(s.cases[i]->value, instructions);
                instructions.push_back(NirJumpIfNotEqual{
                    condition, std::move(case_value), next_label});
                instructions.push_back(NirJump{s.cases[i]->label});
                if (!last) instructions.push_back(NirLabel{next_label});
              }

              instructions.push_back(NirJump{default_target});

              emit_statement(*s.body, instructions);
              instructions.push_back(NirLabel{s.break_label});
            },
            [&](const While &w) {
              instructions.push_back(NirLabel{w.continue_label});

              auto condition = emit_val(w.condition->value, instructions);
              instructions.push_back(
                  NirJumpIfZero{std::move(condition), w.break_label});

              emit_statement(*w.body, instructions);
              instructions.push_back(NirJump{w.continue_label});
              instructions.push_back(NirLabel{w.break_label});
            },
            [&](const DoWhile &d) {
              auto start_label = make_label("start");

              instructions.push_back(NirLabel{start_label});

              emit_statement(*d.body, instructions);

              instructions.push_back(NirLabel{d.continue_label});

              auto condition = emit_val(d.condition->value, instructions);
              instructions.push_back(
                  NirJumpIfNotZero{std::move(condition), start_label});
              instructions.push_back(NirLabel{d.break_label});
            },
            [&](const For &f) {
              auto start_label = make_label("start");

              emit_for_init(f.init, instructions);

              instructions.push_back(NirLabel{start_label});

              if (f.condition) {
                auto condition = emit_val(f.condition->value, instructions);
                instructions.push_back(
                    NirJumpIfZero{std::move(condition), f.break_label});
              }

              emit_statement(*f.body, instructions);

              instructions.push_back(NirLabel{f.continue_label});

              if (f.post) emit_val(f.post->value, instructions);
              instructions.push_back(NirJump{start_label});
              instructions.push_back(NirLabel{f.break_label});
            },
        },
        stmt.value);
  }

  void emit_declaration(const Declaration &d,
                        std::vector<NirInstruction> &instructions) {
    if (!d.init) return;
    auto val = emit_val(*d.init, instructions);
    instructions.push_back(NirCopy{std::move(val), NirVar{d.name}});
  }

  void emit_for_init(const ForInit &init,
                     std::vector<NirInstruction> &instructions) {
    std::visit(
        Overload{
            [&](const InitDecl &d) { emit_declaration(d.decl, instructions); },
            [&](const InitExp &e) {
              if (e.exp) emit_val(*e.exp, instructions);
            },
        },
        init);
  }

  void emit_block_item(const BlockItem &item,
                       std::vector<NirInstruction> &instructions) {
    std::visit(
        Overload{
            [&](const Statement &stmt) { emit_statement(stmt, instructions); },
            [&](const Declaration &d) { emit_declaration(d, instructions); },
        },
        item);
  }

  void emit_block(const Block &block,
                  std::vector<NirInstruction> &instructions) {
    for (const auto &item : block.items) emit_block_item(item, instructions);
  }

  NirFunction emit_function(const Function &func) {
    std::vector<NirInstruction> instructions;
    emit_block(func.body, instructions);

    instructions.push_back(NirReturn{NirConstant{0}});

    return {std::string(func.name), std::move(instructions)};
  }

public:
  NirProgram emit_program(const Program &program) {
    return {emit_function(program.function)};
  }
};

export NirProgram emit_nir(const Program &program) {
  NirEmitter emitter;
  return emitter.emit_program(program);
}