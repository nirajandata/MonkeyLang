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
  using NirConstant =
      std::variant<ConstInt, ConstLong, ConstUInt, ConstULong, ConstDouble>;
  struct NirVar {
    std::string name;
  };
  using NirVal = std::variant<NirConstant, NirVar>;
  struct NirDereferencedPointer {
    NirVal pointer;
  };
  using NirExpResult = std::variant<NirVal, NirDereferencedPointer>;

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
  struct NirSignExtend {
    NirVal src;
    NirVal dst;
  };
  struct NirTruncate {
    NirVal src;
    NirVal dst;
  };
  struct NirZeroExtend {
    NirVal src;
    NirVal dst;
  };
  struct NirDoubleToInt {
    NirVal src;
    NirVal dst;
  };
  struct NirDoubleToUInt {
    NirVal src;
    NirVal dst;
  };
  struct NirIntToDouble {
    NirVal src;
    NirVal dst;
  };
  struct NirUIntToDouble {
    NirVal src;
    NirVal dst;
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
  struct NirGetAddress {
    NirVal src;
    NirVal dst;
  };
  struct NirAddPtr {
    NirVal pointer;
    NirVal index;
    size_t scale;
    NirVal dst;
  };
  struct NirCopyToOffset {
    NirVal src;
    std::string dst;
    size_t offset;
  };
  struct NirLoad {
    NirVal pointer;
    NirVal dst;
  };
  struct NirStore {
    NirVal src;
    NirVal pointer;
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
  struct NirCall {
    std::string name;
    std::vector<NirVal> args;
    NirVal dst;
  };

  using NirInstruction =
      std::variant<NirReturn, NirSignExtend, NirTruncate, NirZeroExtend,
                   NirDoubleToInt, NirDoubleToUInt, NirIntToDouble,
                   NirUIntToDouble, NirUnary, NirBinary, NirCopy, NirGetAddress,
                   NirAddPtr, NirCopyToOffset, NirLoad, NirStore, NirJump,
                   NirJumpIfZero, NirJumpIfNotZero, NirJumpIfNotEqual,
                   NirLabel, NirCall>;

  struct NirFunction {
    std::string name;
    bool global;
    std::vector<std::string> params;
    std::vector<NirInstruction> instructions;
  };
  struct NirStaticVariable {
    std::string name;
    bool global;
    Type type;
    std::vector<StaticInit> init;
  };
  using NirTopLevel = std::variant<NirFunction, NirStaticVariable>;
  struct NirProgram {
    std::vector<NirTopLevel> top_levels;
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
  static size_t type_size(const Type &type) {
    switch (type.kind) {
      case TypeKind::Int:
      case TypeKind::UInt: return 4;
      case TypeKind::Long:
      case TypeKind::ULong:
      case TypeKind::Double:
      case TypeKind::Pointer: return 8;
      case TypeKind::Array:
        return type.referenced ? type.size * type_size(*type.referenced) : 0;
      case TypeKind::Function: return 0;
    }
    std::unreachable();
  }

  NirVar make_tacky_variable(const Type &type) {
    std::string name = "tmp." + std::to_string(next_name_id());
    symbol_table().add(name, type, Symbol::LocalAttr{});
    return NirVar{std::move(name)};
  }

  std::string make_label(std::string_view prefix) {
    return std::string(prefix) + "." + std::to_string(next_name_id());
  }

  NirVal emit_and(const Binary &b,
                    std::vector<NirInstruction> &instructions) {
    NirVal dst = make_tacky_variable(Type::int_type());
    auto false_label = make_label("and_false");
    auto end_label = make_label("and_end");

    auto left = emit_val_and_convert(*b.left, instructions);
    instructions.push_back(NirJumpIfZero{left, false_label});

    auto right = emit_val_and_convert(*b.right, instructions);
    instructions.push_back(NirJumpIfZero{right, false_label});

    instructions.push_back(NirCopy{NirConstant{ConstInt{1, 0}}, dst});
    instructions.push_back(NirJump{end_label});
    instructions.push_back(NirLabel{false_label});
    instructions.push_back(NirCopy{NirConstant{ConstInt{0, 0}}, dst});
    instructions.push_back(NirLabel{end_label});

    return dst;
  }

  NirVal emit_or(const Binary &b,
                   std::vector<NirInstruction> &instructions) {
    NirVal dst = make_tacky_variable(Type::int_type());
    auto true_label = make_label("or_true");
    auto end_label = make_label("or_end");

    auto left = emit_val_and_convert(*b.left, instructions);
    instructions.push_back(NirJumpIfNotZero{left, true_label});

    auto right = emit_val_and_convert(*b.right, instructions);
    instructions.push_back(NirJumpIfNotZero{right, true_label});

    instructions.push_back(NirCopy{NirConstant{ConstInt{0, 0}}, dst});
    instructions.push_back(NirJump{end_label});
    instructions.push_back(NirLabel{true_label});
    instructions.push_back(NirCopy{NirConstant{ConstInt{1, 0}}, dst});
    instructions.push_back(NirLabel{end_label});

    return dst;
  }

  NirVal emit_conditional(const Conditional &c, const Type &type,
                          std::vector<NirInstruction> &instructions) {
    NirVal dst = make_tacky_variable(type);
    auto then_label = make_label("then");
    auto else_label = make_label("else");
    auto end_label = make_label("end");

    auto condition = emit_val_and_convert(*c.condition, instructions);
    instructions.push_back(NirJumpIfZero{std::move(condition), else_label});

    auto then_val = emit_val_and_convert(*c.then_exp, instructions);
    instructions.push_back(NirCopy{std::move(then_val), dst});
    instructions.push_back(NirJump{end_label});
    instructions.push_back(NirLabel{else_label});

    auto else_val = emit_val_and_convert(*c.else_exp, instructions);
    instructions.push_back(NirCopy{std::move(else_val), dst});

    instructions.push_back(NirLabel{then_label});
    instructions.push_back(NirLabel{end_label});
    return dst;
  }

  NirExpResult emit_val(const Exp &exp,
                        std::vector<NirInstruction> &instructions) {
    return std::visit(
        Overload{[](const ConstInt &c) -> NirExpResult {
                   return NirConstant{c};
                 },
                 [](const ConstLong &c) -> NirExpResult {
                   return NirConstant{c};
                 },
                 [](const ConstUInt &c) -> NirExpResult {
                   return NirConstant{c};
                 },
                 [](const ConstULong &c) -> NirExpResult {
                   return NirConstant{c};
                 },
                 [](const ConstDouble &c) -> NirExpResult {
                   return NirConstant{c};
                 },

                 [](const Var &v) -> NirExpResult {
                   return NirVar{v.name};
                 },

                 [&](const Cast &c) -> NirExpResult {
                   NirVal src = emit_val_and_convert(*c.exp, instructions);
                   if (c.target_type == c.exp->type) return src;
                   NirVar dst = make_tacky_variable(c.target_type);
                   if (c.target_type.kind == TypeKind::Double) {
                     if (c.exp->type.kind == TypeKind::UInt ||
                         c.exp->type.kind == TypeKind::ULong)
                       instructions.push_back(NirUIntToDouble{src, dst});
                     else
                       instructions.push_back(NirIntToDouble{src, dst});
                     return dst;
                   }
                   if (c.exp->type.kind == TypeKind::Double) {
                     if (c.target_type.kind == TypeKind::UInt ||
                         c.target_type.kind == TypeKind::ULong)
                       instructions.push_back(NirDoubleToUInt{src, dst});
                     else
                       instructions.push_back(NirDoubleToInt{src, dst});
                     return dst;
                   }
                   const bool source_is_wide =
                       c.exp->type.kind == TypeKind::Long ||
                       c.exp->type.kind == TypeKind::ULong ||
                       c.exp->type.kind == TypeKind::Pointer;
                   const bool target_is_wide =
                       c.target_type.kind == TypeKind::Long ||
                       c.target_type.kind == TypeKind::ULong ||
                       c.target_type.kind == TypeKind::Pointer;
                   if (source_is_wide == target_is_wide) {
                     instructions.push_back(NirCopy{src, dst});
                   } else if (target_is_wide && c.exp->type.kind == TypeKind::Int) {
                     instructions.push_back(NirSignExtend{src, dst});
                   } else if (target_is_wide) {
                     instructions.push_back(NirZeroExtend{src, dst});
                   } else {
                     instructions.push_back(NirTruncate{src, dst});
                   }
                   return dst;
                 },

                 [&](const Assignment &a) -> NirExpResult {
                   auto left = emit_val(*a.left, instructions);
                   auto right = emit_val_and_convert(*a.right, instructions);
                   if (auto *object = std::get_if<NirVal>(&left)) {
                     instructions.push_back(NirCopy{right, *object});
                   } else {
                     instructions.push_back(NirStore{
                         right, std::get<NirDereferencedPointer>(left).pointer});
                   }
                   return right;
                 },

                 [&](const CompoundAssignment &a) -> NirExpResult {
                   auto lvalue = emit_val(*a.left, instructions);
                   NirVal left;
                   NirVal pointer;
                   if (auto *object = std::get_if<NirVal>(&lvalue)) {
                     left = *object;
                   } else {
                     pointer =
                         std::get<NirDereferencedPointer>(lvalue).pointer;
                     left = make_tacky_variable(a.left->type);
                     instructions.push_back(NirLoad{pointer, left});
                   }
                   auto old = NirVal{make_tacky_variable(a.left->type)};
                   instructions.push_back(NirCopy{left, old});
                   auto right = emit_val_and_convert(*a.right, instructions);
                   auto dst = NirVal{make_tacky_variable(a.left->type)};

                   auto op = std::visit(
                       [&](const auto &value) {
                         using OpType = std::decay_t<decltype(value)>;
                         return reflect_to_enum<OpType, NirBinaryOp>();
                       },
                       a.op);
                   instructions.push_back(
                       NirBinary{op, old, std::move(right), dst});
                   if (std::holds_alternative<NirDereferencedPointer>(lvalue)) {
                     instructions.push_back(NirStore{dst, pointer});
                   } else {
                     instructions.push_back(NirCopy{dst, left});
                   }
                   return dst;
                 },

                 [&](const IncDec &e) -> NirExpResult {
                   auto lvalue = emit_val(*e.exp, instructions);
                   NirVal value;
                   NirVal pointer;
                   if (auto *object = std::get_if<NirVal>(&lvalue)) {
                     value = *object;
                   } else {
                     pointer =
                         std::get<NirDereferencedPointer>(lvalue).pointer;
                     value = make_tacky_variable(e.exp->type);
                     instructions.push_back(NirLoad{pointer, value});
                   }
                   auto next = NirVal{make_tacky_variable(e.exp->type)};
                   auto old = e.postfix
                                  ? NirVal{make_tacky_variable(e.exp->type)}
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
                       NirBinary{op, value,
                                 NirConstant{ConstInt{1, e.line}}, next});
                   if (std::holds_alternative<NirDereferencedPointer>(lvalue)) {
                     instructions.push_back(NirStore{next, pointer});
                   } else {
                     instructions.push_back(NirCopy{next, value});
                   }
                   return e.postfix ? old : next;
                 },

                 [&](const Unary &u) -> NirExpResult {
                   return std::visit(
                       [&](const auto &op) -> NirExpResult {
                         using OpType = std::decay_t<decltype(op)>;

                         auto src = emit_val_and_convert(*u.exp, instructions);
                         NirVal dst = make_tacky_variable(exp.type);
                         instructions.push_back(
                             NirUnary{reflect_to_enum<OpType, NirUnaryOp>(),
                                        std::move(src), dst});
                         return dst;
                       },
                       u.op);
                 },

                 [&](const Binary &b) -> NirExpResult {
                   return std::visit(
                       [&](const auto &op) -> NirExpResult {
                         using OpType = std::decay_t<decltype(op)>;

                         if constexpr (std::is_same_v<OpType, And>) {
                           return emit_and(b, instructions);
                         } else if constexpr (std::is_same_v<OpType, Or>) {
                           return emit_or(b, instructions);
                         } else {
                           const bool left_pointer =
                               b.left->type.kind == TypeKind::Pointer;
                           const bool right_pointer =
                               b.right->type.kind == TypeKind::Pointer;
                           auto left =
                               emit_val_and_convert(*b.left, instructions);
                           auto right =
                               emit_val_and_convert(*b.right, instructions);
                           NirVal dst = make_tacky_variable(exp.type);

                           if constexpr (std::is_same_v<OpType, Add> ||
                                         std::is_same_v<OpType, Subtract>) {
                             if (left_pointer &&
                                 (!right_pointer ||
                                  std::is_same_v<OpType, Add>)) {
                               NirVal index = std::move(right);
                               if constexpr (std::is_same_v<OpType,
                                                             Subtract>) {
                                 NirVal negated =
                                     make_tacky_variable(Type::long_type());
                                 instructions.push_back(NirUnary{
                                     NirUnaryOp::Negate, index, negated});
                                 index = std::move(negated);
                               }
                               instructions.push_back(NirAddPtr{
                                   std::move(left), std::move(index),
                                   type_size(*b.left->type.referenced), dst});
                               return dst;
                             }
                             if constexpr (std::is_same_v<OpType, Add>) {
                               if (right_pointer) {
                                 instructions.push_back(NirAddPtr{
                                     std::move(right), std::move(left),
                                     type_size(*b.right->type.referenced),
                                     dst});
                                 return dst;
                               }
                             }
                             if constexpr (std::is_same_v<OpType, Subtract>) {
                               if (left_pointer && right_pointer) {
                                 NirVal byte_difference =
                                     make_tacky_variable(Type::long_type());
                                 instructions.push_back(NirBinary{
                                     NirBinaryOp::Subtract, left, right,
                                     byte_difference});
                                 const size_t scale =
                                     type_size(*b.left->type.referenced);
                                 if (scale == 1) {
                                   instructions.push_back(
                                       NirCopy{byte_difference, dst});
                                 } else {
                                   instructions.push_back(NirBinary{
                                       NirBinaryOp::Divide, byte_difference,
                                       NirConstant{ConstLong{
                                           static_cast<int64_t>(scale), 0}},
                                       dst});
                                 }
                                 return dst;
                               }
                             }
                           }
                           instructions.push_back(NirBinary{
                            reflect_to_enum<OpType, NirBinaryOp>(),
                            std::move(left), std::move(right), dst});
                            return dst;
                          }
                        },
                        b.op);
                  },

                  [&](const Conditional &c) -> NirExpResult {
                    return emit_conditional(c, exp.type, instructions);
                  },

                  [&](const FunctionCall &c) -> NirExpResult {
                    std::vector<NirVal> args;
                    args.reserve(c.args.size());
                    for (const auto &arg : c.args)
                      args.push_back(
                          emit_val_and_convert(*arg, instructions));

                    NirVal dst = make_tacky_variable(exp.type);
                    instructions.push_back(
                        NirCall{c.name, std::move(args), dst});
                    return dst;
                  },

                  [&](const Dereference &d) -> NirExpResult {
                    auto pointer = emit_val_and_convert(*d.exp, instructions);
                    return NirDereferencedPointer{std::move(pointer)};
                  },

                  [&](const AddrOf &a) -> NirExpResult {
                    auto object = emit_val(*a.exp, instructions);
                    if (auto *pointer =
                            std::get_if<NirDereferencedPointer>(&object))
                      return std::move(pointer->pointer);
                    NirVal dst = make_tacky_variable(exp.type);
                    instructions.push_back(
                        NirGetAddress{std::get<NirVal>(object), dst});
                    return dst;
                  },

                  [&](const Subscript &s) -> NirExpResult {
                    const bool left_is_pointer =
                        s.left->type.kind == TypeKind::Pointer;
                    const Exp &pointer_exp =
                        left_is_pointer ? *s.left : *s.right;
                    const Exp &index_exp =
                        left_is_pointer ? *s.right : *s.left;
                    auto pointer =
                        emit_val_and_convert(pointer_exp, instructions);
                    auto index =
                        emit_val_and_convert(index_exp, instructions);
                    auto dst = NirVal{make_tacky_variable(
                        Type::pointer(exp.type))};
                    instructions.push_back(NirAddPtr{
                        std::move(pointer), std::move(index),
                        type_size(exp.type), dst});
                    return NirDereferencedPointer{std::move(dst)};
                  }},
         exp.value);
  }

  NirVal emit_val_and_convert(const Exp &exp,
                              std::vector<NirInstruction> &instructions) {
    auto result = emit_val(exp, instructions);
    if (auto *value = std::get_if<NirVal>(&result)) return std::move(*value);
    NirVal dst = make_tacky_variable(exp.type);
    instructions.push_back(
        NirLoad{std::get<NirDereferencedPointer>(result).pointer, dst});
    return dst;
  }

  void emit_statement(const Statement &stmt,
                      std::vector<NirInstruction> &instructions) {
    std::visit(
        Overload{
            [&](const Return &r) {
              instructions.push_back(
                  NirReturn{emit_val_and_convert(r.value, instructions)});
            },
            [&](const Expression &e) { emit_val(e.value, instructions); },
            [&](const Null &) {},
            [&](const If &i) {
              auto then_label = make_label("then");
              auto else_label = make_label("else");
              auto end_label = make_label("end");

              auto condition =
                  emit_val_and_convert(i.condition, instructions);
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
            [&](const Default &d) {
              instructions.push_back(NirLabel{d.label});
              emit_statement(*d.stmt, instructions);
            },
            [&](const Switch &s) {
              const auto &default_target =
                  s.default_case ? s.default_case->label : s.break_label;

              auto condition =
                  emit_val_and_convert(s.condition, instructions);

              for (size_t i = 0; i < s.cases.size(); ++i) {
                const bool last = (i + 1 == s.cases.size());
                auto next_label =
                    last ? default_target : make_label("case_next");

                auto case_value = emit_val_and_convert(
                    s.cases[i]->value, instructions);
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

              auto condition = emit_val_and_convert(
                  w.condition->value, instructions);
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

              auto condition = emit_val_and_convert(
                  d.condition->value, instructions);
              instructions.push_back(
                  NirJumpIfNotZero{std::move(condition), start_label});
              instructions.push_back(NirLabel{d.break_label});
            },
            [&](const For &f) {
              auto start_label = make_label("start");

              emit_for_init(f.init, instructions);

              instructions.push_back(NirLabel{start_label});

              if (f.condition) {
                auto condition = emit_val_and_convert(
                    f.condition->value, instructions);
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

  void emit_variable_declaration(const VariableDeclaration &d,
                                 std::vector<NirInstruction> &instructions) {
    if (d.storage_class || !d.init) return;
    auto emit_init = [&](auto &&self, const Initializer &init, size_t offset,
                         const Type &type) -> void {
      std::visit(
          Overload{
              [&](const SingleInit &single) {
                auto val = emit_val_and_convert(single.exp, instructions);
                if (type.kind == TypeKind::Array) return;
                if (d.var_type.kind != TypeKind::Array)
                  instructions.push_back(
                      NirCopy{std::move(val), NirVar{d.name}});
                else
                  instructions.push_back(
                      NirCopyToOffset{std::move(val), d.name, offset});
              },
              [&](const CompoundInit &compound) {
                if (!type.referenced) return;
                const size_t element_size = type_size(*type.referenced);
                for (size_t i = 0; i < compound.initializers.size(); ++i)
                  self(self, compound.initializers[i],
                       offset + i * element_size, *type.referenced);
              }},
          init.value);
    };
    emit_init(emit_init, *d.init, 0, d.var_type);
  }

  void emit_for_init(const ForInit &init,
                     std::vector<NirInstruction> &instructions) {
    std::visit(
        Overload{
            [&](const InitDecl &d) { emit_variable_declaration(d.decl, instructions); },
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
            [&](const Declaration &d) {
              std::visit(Overload{
                             [&](const FunDecl &f) {
                               if (f.decl.body)
                                 emit_block(*f.decl.body, instructions);
                             },
                             [&](const VarDecl &v) {
                               emit_variable_declaration(v.decl, instructions);
                             },
                         },
                         d);
            },
        },
        item);
  }

  void emit_block(const Block &block,
                  std::vector<NirInstruction> &instructions) {
    for (const auto &item : block.items) emit_block_item(item, instructions);
  }

  NirFunction emit_function(const FunctionDeclaration &func) {
    std::vector<NirInstruction> instructions;
    if (func.body) {
      emit_block(*func.body, instructions);
      instructions.push_back(NirReturn{NirConstant{ConstInt{0, 0}}});
    }

    bool global = true;
    if (const Symbol *symbol = symbol_table().find(func.name)) {
      if (const auto *attrs = std::get_if<Symbol::FunAttr>(&symbol->attrs))
        global = attrs->global;
    }
    return {std::string(func.name), global, func.params,
            std::move(instructions)};
  }

public:
  NirProgram emit_program(const Program &program) {
    std::vector<NirTopLevel> top_levels;
    top_levels.reserve(program.declarations.size() +
                       symbol_table().entries().size());
    for (const auto &declaration : program.declarations) {
      if (const auto *func = std::get_if<FunDecl>(&declaration))
        top_levels.push_back(emit_function(func->decl));
    }

    for (const auto &[name, symbol] : symbol_table().entries()) {
      if (const auto *attrs = std::get_if<Symbol::StaticAttr>(&symbol.attrs)) {
        if (attrs->init.kind == Symbol::InitialValue::Kind::NoInitializer)
          continue;
        std::vector<StaticInit> init = attrs->init.values;
        const auto type_size = [](const auto &self, const Type &type) -> size_t {
          switch (type.kind) {
            case TypeKind::Int:
            case TypeKind::UInt: return 4;
            case TypeKind::Long:
            case TypeKind::ULong:
            case TypeKind::Double:
            case TypeKind::Pointer: return 8;
            case TypeKind::Array:
              return type.referenced
                         ? type.size * self(self, *type.referenced)
                         : 0;
            case TypeKind::Function: return 0;
          }
          std::unreachable();
        };
        if (init.empty())
          init.push_back(ZeroInit{type_size(type_size, symbol.type)});
        if (symbol.type.kind == TypeKind::Array) {
          top_levels.push_back(NirStaticVariable{
              name, attrs->global, symbol.type, std::move(init)});
          continue;
        }
        top_levels.push_back(NirStaticVariable{
            name, attrs->global, symbol.type, std::move(init)});
      }
    }

    return {std::move(top_levels)};
  }
};

export NirProgram emit_nir(const Program &program) {
  NirEmitter emitter;
  return emitter.emit_program(program);
}