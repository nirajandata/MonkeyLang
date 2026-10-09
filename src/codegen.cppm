module;

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

export module codegen;

import ast;
import nir;
import semantic;

export {
  enum class RegId : uint8_t {
    AX, CX, DX, DI, SI, R8, R9, R10, R11,
    XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7, XMM14, XMM15
  };
  enum class AsmType : uint8_t { Longword, Quadword, Double };

  struct Reg { RegId id; };
  struct Imm { int64_t value; };
  struct Pseudo { std::string name; };
  struct Stack { int offset; };
  struct Data { std::string name; bool constant = false; };
  struct Indirect { RegId base; };

  using Operand = std::variant<Imm, Reg, Pseudo, Stack, Data, Indirect>;

  struct Mov {
    AsmType type;
    Operand src;
    Operand dst;
    Mov(Operand src, Operand dst) : type(AsmType::Longword), src(std::move(src)), dst(std::move(dst)) {}
    Mov(AsmType type, Operand src, Operand dst)
        : type(type), src(std::move(src)), dst(std::move(dst)) {}
  };
  struct Movsx { Operand src; Operand dst; };
  struct Movzx { Operand src; Operand dst; };
  struct Lea { Operand src; Operand dst; };
  struct FloatBinary {
    enum class Op : uint8_t { Add, Sub, Mult, Div, Xor } op;
    Operand src;
    Operand dst;
  };
  struct Cvtsi2sd { AsmType type; Operand src; Operand dst; };
  struct Cvttsd2si { AsmType type; Operand src; Operand dst; };
  struct Addl { AsmType type; Operand src; Operand dst; Addl(Operand s, Operand d) : Addl(AsmType::Longword, std::move(s), std::move(d)) {} Addl(AsmType t, Operand s, Operand d) : type(t), src(std::move(s)), dst(std::move(d)) {} };
  struct Subl { AsmType type; Operand src; Operand dst; Subl(Operand s, Operand d) : Subl(AsmType::Longword, std::move(s), std::move(d)) {} Subl(AsmType t, Operand s, Operand d) : type(t), src(std::move(s)), dst(std::move(d)) {} };
  struct Imull { AsmType type; Operand src; Operand dst; Imull(Operand s, Operand d) : Imull(AsmType::Longword, std::move(s), std::move(d)) {} Imull(AsmType t, Operand s, Operand d) : type(t), src(std::move(s)), dst(std::move(d)) {} };
  struct Idivl { AsmType type; Operand operand; Idivl(Operand o) : Idivl(AsmType::Longword, std::move(o)) {} Idivl(AsmType t, Operand o) : type(t), operand(std::move(o)) {} };
  struct Div { AsmType type; Operand operand; };
  struct Cdq { AsmType type = AsmType::Longword; };
  struct Cmpl { AsmType type; Operand src; Operand dst; Cmpl(Operand s, Operand d) : Cmpl(AsmType::Longword, std::move(s), std::move(d)) {} Cmpl(AsmType t, Operand s, Operand d) : type(t), src(std::move(s)), dst(std::move(d)) {} };
  struct Andl { AsmType type; Operand src; Operand dst; Andl(Operand s, Operand d) : Andl(AsmType::Longword, std::move(s), std::move(d)) {} Andl(AsmType t, Operand s, Operand d) : type(t), src(std::move(s)), dst(std::move(d)) {} };
  struct Orl { AsmType type; Operand src; Operand dst; Orl(Operand s, Operand d) : Orl(AsmType::Longword, std::move(s), std::move(d)) {} Orl(AsmType t, Operand s, Operand d) : type(t), src(std::move(s)), dst(std::move(d)) {} };
  struct Xorl { AsmType type; Operand src; Operand dst; Xorl(Operand s, Operand d) : Xorl(AsmType::Longword, std::move(s), std::move(d)) {} Xorl(AsmType t, Operand s, Operand d) : type(t), src(std::move(s)), dst(std::move(d)) {} };
  struct Shll { AsmType type; Operand operand; Shll(Operand o) : Shll(AsmType::Longword, std::move(o)) {} Shll(AsmType t, Operand o) : type(t), operand(std::move(o)) {} };
  struct Sarl { AsmType type; Operand operand; Sarl(Operand o) : Sarl(AsmType::Longword, std::move(o)) {} Sarl(AsmType t, Operand o) : type(t), operand(std::move(o)) {} };
  struct Shrl { AsmType type; Operand operand; };

  enum class AsmUnaryOp : uint8_t { Neg, Not };

  struct AsmUnary {
    AsmUnaryOp op;
    AsmType type;
    Operand operand;
    AsmUnary(AsmUnaryOp op, Operand operand)
        : op(op), type(AsmType::Longword), operand(std::move(operand)) {}
    AsmUnary(AsmUnaryOp op, AsmType type, Operand operand)
        : op(op), type(type), operand(std::move(operand)) {}
  };

  enum class CondCode : uint8_t { E, NE, G, GE, L, LE, A, AE, B, BE, P, NP };

  struct Jmp { std::string target; };
  struct JmpCC { CondCode cc; std::string target; };
  struct SetCC { CondCode cc; Operand operand; };
  struct AsmLabel { std::string name; };
  struct AllocateStack { int bytes; };
  struct DeallocateStack { int bytes; };
  struct Pushq { Operand operand; };
  struct Call { std::string name; };
  struct Ret {};
  struct PushDouble { Operand operand; };
  struct AsmStaticConstant {
    std::string name;
    int alignment;
    DoubleInit init;
  };

  using AsmInstruction =
      std::variant<Mov, Movsx, Movzx, Lea, FloatBinary, Cvtsi2sd, Cvttsd2si,
                   AsmUnary, Addl, Subl, Imull, Idivl, Div, Cdq, Cmpl, Andl,
                   Orl, Xorl, Shll, Sarl, Shrl, Jmp, JmpCC, SetCC, AsmLabel,
                   AllocateStack, DeallocateStack, Pushq, PushDouble, Call, Ret>;

  struct AsmFunction {
    std::string name;
    bool global;
    std::vector<std::string> params;
    std::vector<AsmInstruction> instructions;
  };

  struct AsmStaticVariable {
    std::string name;
    bool global;
    Type type;
    StaticInit init;
  };

  using AsmTopLevel =
      std::variant<AsmFunction, AsmStaticVariable, AsmStaticConstant>;

  struct AsmProgram {
    std::vector<AsmTopLevel> top_levels;
  };
}

using ConstantPool = std::vector<AsmStaticConstant>;

static std::string constant_name(double value, ConstantPool &constants) {
  const uint64_t bits = std::bit_cast<uint64_t>(value);
  for (const auto &constant : constants) {
    if (std::bit_cast<uint64_t>(constant.init.value) == bits)
      return constant.name;
  }
  std::string name = "LC" + std::to_string(constants.size());
  const int alignment = value == 0.0 && (bits >> 63) != 0 ? 16 : 8;
  constants.push_back({name, alignment, DoubleInit{value}});
  return name;
}

static Operand nir_val_to_operand(const NirVal &val,
                                  ConstantPool &constants) {
  return std::visit(Overload{
    [&](const NirConstant &c) -> Operand {
      return std::visit(
          Overload{
              [&](const ConstDouble &constant) -> Operand {
                return Data{constant_name(constant.value, constants), true};
              },
              [](const ConstULong &constant) -> Operand {
                return Imm{std::bit_cast<int64_t>(constant.value)};
              },
              [](const auto &constant) -> Operand {
                return Imm{static_cast<int64_t>(constant.value)};
              }},
          c);
    },
    [](const NirVar &v) -> Operand { return Pseudo{v.name}; }
  }, val);
}

static AsmType asm_type(const Type &type) {
  if (type.kind == TypeKind::Double) return AsmType::Double;
  return type.kind == TypeKind::Long || type.kind == TypeKind::ULong ||
                 type.kind == TypeKind::Pointer
             ? AsmType::Quadword
             : AsmType::Longword;
}

static Type type_of(const NirVal &value) {
  return std::visit(
      Overload{
          [](const NirConstant &constant) {
            return std::visit(
                Overload{[](const ConstInt &) { return Type::int_type(); },
                         [](const ConstLong &) { return Type::long_type(); },
                         [](const ConstUInt &) { return Type::uint_type(); },
                         [](const ConstULong &) { return Type::ulong_type(); },
                         [](const ConstDouble &) { return Type::double_type(); }},
                constant);
          },
          [](const NirVar &variable) {
            if (const Symbol *symbol = symbol_table().find(variable.name))
              return symbol->type;
            return Type::int_type();
          }},
      value);
}

constexpr size_t num_arg_registers = 6;

constexpr RegId arg_registers[num_arg_registers] = {
    RegId::DI, RegId::SI, RegId::DX, RegId::CX, RegId::R8, RegId::R9};

static Operand param_source(size_t index) {
  if (index < num_arg_registers)
    return Reg{arg_registers[index]};
  return Stack{16 + 8 * static_cast<int>(index - num_arg_registers)};
}


static bool is_memory(const Operand &o) {
  return std::holds_alternative<Stack>(o) || std::holds_alternative<Data>(o) ||
         std::holds_alternative<Indirect>(o);
}

static void track_stack(const Operand &o, int &stack_bytes) {
  if (auto *s = std::get_if<Stack>(&o)) {
    stack_bytes = std::max(stack_bytes, -s->offset);
  }
}

static void track_stack_ops(const Operand &a, const Operand &b, int &sb) {
  track_stack(a, sb);
  track_stack(b, sb);
}

static bool is_relational(NirBinaryOp op) {
  return op == NirBinaryOp::Equal || op == NirBinaryOp::NotEqual ||
         op == NirBinaryOp::LessThan || op == NirBinaryOp::LessOrEqual ||
         op == NirBinaryOp::GreaterThan || op == NirBinaryOp::GreaterOrEqual;
}

static CondCode convert_relational(NirBinaryOp op, bool is_unsigned) {
  switch (op) {
    case NirBinaryOp::Equal: return CondCode::E;
    case NirBinaryOp::NotEqual: return CondCode::NE;
    case NirBinaryOp::LessThan:
      return is_unsigned ? CondCode::B : CondCode::L;
    case NirBinaryOp::LessOrEqual:
      return is_unsigned ? CondCode::BE : CondCode::LE;
    case NirBinaryOp::GreaterThan:
      return is_unsigned ? CondCode::A : CondCode::G;
    case NirBinaryOp::GreaterOrEqual:
      return is_unsigned ? CondCode::AE : CondCode::GE;
    default: std::unreachable();
  }
}

static void emit_function_call(const NirCall &c,
                               std::vector<AsmInstruction> &instructions,
                               ConstantPool &constants) {
  std::vector<Operand> args;
  args.reserve(c.args.size());
  for (const auto &arg : c.args)
    args.push_back(nir_val_to_operand(arg, constants));

  size_t int_reg = 0, double_reg = 0, stack_args = 0;
  std::vector<bool> on_stack(args.size());
  for (size_t i = 0; i < args.size(); ++i) {
    if (type_of(c.args[i]).kind == TypeKind::Double) {
      if (double_reg++ >= 8) on_stack[i] = true, ++stack_args;
    } else if (int_reg++ >= 6) {
      on_stack[i] = true;
      ++stack_args;
    }
  }
  const int stack_padding = stack_args % 2 != 0 ? 8 : 0;
  if (stack_padding != 0)
    instructions.push_back(AllocateStack{stack_padding});

  int_reg = double_reg = 0;
  for (size_t i = 0; i < args.size(); ++i) {
    if (on_stack[i]) continue;
    if (type_of(c.args[i]).kind == TypeKind::Double) {
      const auto xmm = static_cast<RegId>(
          static_cast<uint8_t>(RegId::XMM0) + double_reg++);
      instructions.push_back(Mov{AsmType::Double, args[i], Reg{xmm}});
    } else {
      instructions.push_back(
          Mov{asm_type(type_of(c.args[i])), args[i],
              Reg{arg_registers[int_reg++]}});
    }
  }

  for (size_t i = args.size(); i > 0; --i) {
    if (!on_stack[i - 1]) continue;
    const Operand &arg = args[i - 1];
    if (type_of(c.args[i - 1]).kind == TypeKind::Double) {
      instructions.push_back(PushDouble{arg});
    } else if (std::holds_alternative<Reg>(arg) ||
               std::holds_alternative<Imm>(arg)) {
      instructions.push_back(Pushq{arg});
    } else {
      instructions.push_back(
          Mov{asm_type(type_of(c.args[i - 1])), arg, Reg{RegId::AX}});
      instructions.push_back(Pushq{Reg{RegId::AX}});
    }
  }

  instructions.push_back(Call{c.name});

  const int bytes_to_remove =
      static_cast<int>(8 * stack_args) + stack_padding;
  if (bytes_to_remove != 0)
    instructions.push_back(DeallocateStack{bytes_to_remove});

  if (type_of(c.dst).kind == TypeKind::Double)
    instructions.push_back(
        Mov{AsmType::Double, Reg{RegId::XMM0},
            nir_val_to_operand(c.dst, constants)});
  else
    instructions.push_back(Mov{asm_type(type_of(c.dst)), Reg{RegId::AX},
                               nir_val_to_operand(c.dst, constants)});
}

static AsmFunction nir_to_asm(const NirFunction &func,
                              ConstantPool &constants) {
  std::vector<AsmInstruction> instructions;
  auto op = [&](const NirVal &value) {
    return nir_val_to_operand(value, constants);
  };

  size_t int_reg = 0, double_reg = 0;
  int stack_offset = 16;
  for (size_t i = 0; i < func.params.size(); ++i) {
    const Type type = symbol_table().find(func.params[i])
                          ? symbol_table().get(func.params[i]).type
                          : Type::int_type();
    Operand source;
    if (type.kind == TypeKind::Double && double_reg < 8) {
      source = Reg{static_cast<RegId>(
          static_cast<uint8_t>(RegId::XMM0) + double_reg++)};
    } else if (type.kind != TypeKind::Double && int_reg < 6) {
      source = Reg{arg_registers[int_reg++]};
    } else {
      source = Stack{stack_offset};
      stack_offset += 8;
    }
    instructions.push_back(Mov{asm_type(type), std::move(source),
                               Pseudo{func.params[i]}});
  }

  for (const auto &instr : func.instructions) {
    std::visit(Overload{
        [&](const NirReturn &r) {
          const bool is_double = type_of(r.val).kind == TypeKind::Double;
          instructions.push_back(Mov{asm_type(type_of(r.val)), op(r.val),
                                     Reg{is_double ? RegId::XMM0 : RegId::AX}});
          instructions.push_back(Ret{});
        },
        [&](const NirSignExtend &c) {
          instructions.push_back(Movsx{op(c.src), op(c.dst)});
        },
        [&](const NirZeroExtend &c) {
          instructions.push_back(Movzx{op(c.src), op(c.dst)});
        },
        [&](const NirDoubleToInt &c) {
          instructions.push_back(
              Cvttsd2si{asm_type(type_of(c.dst)), op(c.src), op(c.dst)});
        },
        [&](const NirDoubleToUInt &c) {
          const Type target = type_of(c.dst);
          const bool wide = target.kind == TypeKind::ULong;
          const std::string large_label =
              "double_to_uint_large." + std::to_string(next_name_id());
          const std::string end_label =
              "double_to_uint_end." + std::to_string(next_name_id());
          if (!wide) {
            instructions.push_back(
                Cvttsd2si{AsmType::Quadword, op(c.src), Reg{RegId::R11}});
            instructions.push_back(
                Mov{AsmType::Longword, Reg{RegId::R11}, op(c.dst)});
          } else {
            Operand upper = Data{constant_name(9223372036854775808.0,
                                               constants),
                                 true};
            instructions.push_back(Cmpl{AsmType::Double, upper, op(c.src)});
            instructions.push_back(JmpCC{CondCode::AE, large_label});
            instructions.push_back(
                Cvttsd2si{AsmType::Quadword, op(c.src), op(c.dst)});
            instructions.push_back(Jmp{end_label});
            instructions.push_back(AsmLabel{large_label});
            instructions.push_back(Mov{AsmType::Double, op(c.src),
                                       Reg{RegId::XMM14}});
            instructions.push_back(FloatBinary{
                FloatBinary::Op::Sub, upper, Reg{RegId::XMM14}});
            instructions.push_back(Cvttsd2si{AsmType::Quadword,
                                             Reg{RegId::XMM14},
                                             Reg{RegId::R11}});
            instructions.push_back(Mov{
                AsmType::Quadword, Imm{INT64_MIN}, Reg{RegId::R10}});
            instructions.push_back(
                Addl{AsmType::Quadword, Reg{RegId::R10}, Reg{RegId::R11}});
            instructions.push_back(Mov{AsmType::Quadword, Reg{RegId::R11},
                                       op(c.dst)});
            instructions.push_back(AsmLabel{end_label});
          }
        },
        [&](const NirIntToDouble &c) {
          instructions.push_back(
              Cvtsi2sd{asm_type(type_of(c.src)), op(c.src), op(c.dst)});
        },
        [&](const NirUIntToDouble &c) {
          const Type source = type_of(c.src);
          if (source.kind == TypeKind::UInt) {
            instructions.push_back(
                Movzx{op(c.src), Reg{RegId::R10}});
            instructions.push_back(
                Cvtsi2sd{AsmType::Quadword, Reg{RegId::R10}, op(c.dst)});
          } else {
            const std::string low_label =
                "uint_to_double_low." + std::to_string(next_name_id());
            const std::string end_label =
                "uint_to_double_end." + std::to_string(next_name_id());
            instructions.push_back(
                Cmpl{AsmType::Quadword, Imm{0}, op(c.src)});
            instructions.push_back(JmpCC{CondCode::L, low_label});
            instructions.push_back(
                Cvtsi2sd{AsmType::Quadword, op(c.src), op(c.dst)});
            instructions.push_back(Jmp{end_label});
            instructions.push_back(AsmLabel{low_label});
            instructions.push_back(
                Mov{AsmType::Quadword, op(c.src), Reg{RegId::R10}});
            instructions.push_back(
                Mov{AsmType::Quadword, Reg{RegId::R10}, Reg{RegId::R11}});
            instructions.push_back(
                Mov{AsmType::Longword, Imm{1}, Reg{RegId::CX}});
            instructions.push_back(Shrl{AsmType::Quadword, Reg{RegId::R11}});
            instructions.push_back(
                Andl{AsmType::Quadword, Imm{1}, Reg{RegId::R10}});
            instructions.push_back(
                Orl{AsmType::Quadword, Reg{RegId::R10}, Reg{RegId::R11}});
            instructions.push_back(
                Cvtsi2sd{AsmType::Quadword, Reg{RegId::R11}, op(c.dst)});
            instructions.push_back(FloatBinary{
                FloatBinary::Op::Add, op(c.dst), op(c.dst)});
            instructions.push_back(AsmLabel{end_label});
          }
        },
        [&](const NirTruncate &c) {
          instructions.push_back(
              Mov{AsmType::Longword, op(c.src), op(c.dst)});
        },
        [&](const NirUnary &u) {
          const AsmType type = asm_type(type_of(u.src));
          if (type == AsmType::Double &&
              u.op == NirUnaryOp::Negate) {
            instructions.push_back(
                Mov{type, op(u.src), op(u.dst)});
            instructions.push_back(FloatBinary{
                FloatBinary::Op::Xor,
                Data{constant_name(-0.0, constants), true}, op(u.dst)});
            return;
          }
          if (u.op == NirUnaryOp::Not) {
            if (type == AsmType::Double) {
              instructions.push_back(
                  FloatBinary{FloatBinary::Op::Xor, Reg{RegId::XMM14},
                              Reg{RegId::XMM14}});
              instructions.push_back(Cmpl{type, Reg{RegId::XMM14}, op(u.src)});
              instructions.push_back(
                  Mov{AsmType::Longword, Imm{0}, Reg{RegId::R11}});
              instructions.push_back(
                  Mov{AsmType::Longword, Imm{0}, Reg{RegId::R10}});
              instructions.push_back(SetCC{CondCode::E, Reg{RegId::R11}});
              instructions.push_back(SetCC{CondCode::NP, Reg{RegId::R10}});
              instructions.push_back(
                  Andl{AsmType::Longword, Reg{RegId::R10}, Reg{RegId::R11}});
              instructions.push_back(
                  Mov{AsmType::Longword, Reg{RegId::R11}, op(u.dst)});
              return;
            }
            instructions.push_back(Mov{type, op(u.src), Reg{RegId::R10}});
            instructions.push_back(Cmpl{type, Imm{0}, Reg{RegId::R10}});
            instructions.push_back(
                Mov{AsmType::Longword, Imm{0}, Reg{RegId::R11}});
            instructions.push_back(SetCC{CondCode::E, Reg{RegId::R11}});
            instructions.push_back(Mov{AsmType::Longword, Reg{RegId::R11},
                                       op(u.dst)});
          } else {
            instructions.push_back(Mov{type, op(u.src), op(u.dst)});
            instructions.push_back(AsmUnary{
                u.op == NirUnaryOp::Negate ? AsmUnaryOp::Neg : AsmUnaryOp::Not,
                type, op(u.dst)});
          }
        },
        [&](const NirBinary &b) {
          auto src1 = op(b.src1);
          auto src2 = op(b.src2);
          auto dst = op(b.dst);
          const AsmType type = asm_type(type_of(b.src1));

          if (type == AsmType::Double && is_relational(b.op)) {
            instructions.push_back(Mov{type, src1, Reg{RegId::XMM15}});
            instructions.push_back(Cmpl{type, src2, Reg{RegId::XMM15}});
            instructions.push_back(
                Mov{AsmType::Longword, Imm{0}, Reg{RegId::R11}});
            instructions.push_back(
                Mov{AsmType::Longword, Imm{0}, Reg{RegId::R10}});
            const CondCode cc = convert_relational(b.op, true);
            instructions.push_back(SetCC{cc, Reg{RegId::R11}});
            if (b.op == NirBinaryOp::NotEqual) {
              instructions.push_back(SetCC{CondCode::P, Reg{RegId::R10}});
              instructions.push_back(Orl{AsmType::Longword, Reg{RegId::R10},
                                          Reg{RegId::R11}});
            } else {
              instructions.push_back(SetCC{CondCode::NP, Reg{RegId::R10}});
              instructions.push_back(Andl{AsmType::Longword, Reg{RegId::R10},
                                           Reg{RegId::R11}});
            }
            instructions.push_back(
                Mov{AsmType::Longword, Reg{RegId::R11}, dst});
            return;
          }

          if (type == AsmType::Double) {
            FloatBinary::Op op_kind;
            switch (b.op) {
              case NirBinaryOp::Add: op_kind = FloatBinary::Op::Add; break;
              case NirBinaryOp::Subtract: op_kind = FloatBinary::Op::Sub; break;
              case NirBinaryOp::Multiply: op_kind = FloatBinary::Op::Mult; break;
              case NirBinaryOp::Divide: op_kind = FloatBinary::Op::Div; break;
              default: return;
            }
            instructions.push_back(Mov{type, src1, dst});
            instructions.push_back(FloatBinary{op_kind, src2, dst});
            return;
          }

          if (is_relational(b.op)) {
            const Type operand_type = type_of(b.src1);
            const bool is_unsigned =
                operand_type.kind == TypeKind::UInt ||
                operand_type.kind == TypeKind::ULong;
            instructions.push_back(Cmpl{type, src2, src1});
            instructions.push_back(
                Mov{AsmType::Longword, Imm{0}, Reg{RegId::R11}});
            instructions.push_back(SetCC{
                convert_relational(b.op, is_unsigned), Reg{RegId::R11}});
            instructions.push_back(
                Mov{AsmType::Longword, Reg{RegId::R11}, dst});
            return;
          }

          auto ensure_r10 = [&](const Operand &s) {
            if (auto *r = std::get_if<Reg>(&s); !r || r->id != RegId::R10) {
              instructions.push_back(Mov{type, s, Reg{RegId::R10}});
            }
          };

          switch (b.op) {
            case NirBinaryOp::Add: instructions.push_back(Mov{type, src1, dst}); ensure_r10(src2); instructions.push_back(Addl{type, Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::BitwiseAnd: instructions.push_back(Mov{type, src1, dst}); ensure_r10(src2); instructions.push_back(Andl{type, Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::BitwiseOr: instructions.push_back(Mov{type, src1, dst}); ensure_r10(src2); instructions.push_back(Orl{type, Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::BitwiseXor: instructions.push_back(Mov{type, src1, dst}); ensure_r10(src2); instructions.push_back(Xorl{type, Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::Subtract: instructions.push_back(Mov{type, src1, dst}); ensure_r10(src2); instructions.push_back(Subl{type, Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::Multiply: instructions.push_back(Mov{type, src1, dst}); ensure_r10(src2); instructions.push_back(Imull{type, Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::Divide:
            case NirBinaryOp::Remainder:
              instructions.push_back(Mov{type, src1, Reg{RegId::AX}});
              if (type_of(b.src1).kind == TypeKind::UInt ||
                  type_of(b.src1).kind == TypeKind::ULong) {
                instructions.push_back(
                    Mov{type, Imm{0}, Reg{RegId::DX}});
              } else {
                instructions.push_back(Cdq{type});
              }
              ensure_r10(src2);
              if (type_of(b.src1).kind == TypeKind::UInt ||
                  type_of(b.src1).kind == TypeKind::ULong)
                instructions.push_back(Div{type, Reg{RegId::R10}});
              else
                instructions.push_back(Idivl{type, Reg{RegId::R10}});
              instructions.push_back(Mov{
                  type, b.op == NirBinaryOp::Divide ? Reg{RegId::AX}
                                                    : Reg{RegId::DX},
                  dst});
              break;
            case NirBinaryOp::ShiftLeft:
            case NirBinaryOp::ShiftRight:
              instructions.push_back(
                  Mov{AsmType::Longword, src2, Reg{RegId::CX}});
              instructions.push_back(Mov{type, src1, dst});
              if (b.op == NirBinaryOp::ShiftLeft)
                instructions.push_back(Shll{type, dst});
              else if (type_of(b.src1).kind == TypeKind::UInt ||
                       type_of(b.src1).kind == TypeKind::ULong)
                instructions.push_back(Shrl{type, dst});
              else
                instructions.push_back(Sarl{type, dst});
              break;
            default: break;
          }
        },
        [&](const NirCopy &c) {
          instructions.push_back(
              Mov{asm_type(type_of(c.dst)), op(c.src), op(c.dst)});
        },
        [&](const NirGetAddress &a) {
          instructions.push_back(Lea{op(a.src), op(a.dst)});
        },
        [&](const NirLoad &l) {
          const Type loaded_type = type_of(l.dst);
          instructions.push_back(
              Mov{AsmType::Quadword, op(l.pointer), Reg{RegId::R10}});
          instructions.push_back(Mov{asm_type(loaded_type),
                                     Indirect{RegId::R10}, op(l.dst)});
        },
        [&](const NirStore &s) {
          const Type stored_type = type_of(s.src);
          instructions.push_back(
              Mov{AsmType::Quadword, op(s.pointer), Reg{RegId::R10}});
          instructions.push_back(Mov{asm_type(stored_type), op(s.src),
                                     Indirect{RegId::R10}});
        },
        [&](const NirJump &j) { instructions.push_back(Jmp{j.target}); },
        [&](const NirJumpIfZero &j) {
          const AsmType type = asm_type(type_of(j.condition));
          if (type == AsmType::Double) {
            const std::string nonzero_label =
                "double_zero_skip." + std::to_string(next_name_id());
            instructions.push_back(FloatBinary{
                FloatBinary::Op::Xor, Reg{RegId::XMM14}, Reg{RegId::XMM14}});
            instructions.push_back(Mov{type, op(j.condition),
                                       Reg{RegId::XMM15}});
            instructions.push_back(Cmpl{type, Reg{RegId::XMM14},
                                         Reg{RegId::XMM15}});
            instructions.push_back(JmpCC{CondCode::P, nonzero_label});
            instructions.push_back(JmpCC{CondCode::E, j.target});
            instructions.push_back(AsmLabel{nonzero_label});
            return;
          }
          instructions.push_back(Cmpl{type, op(j.condition), Imm{0}});
          instructions.push_back(JmpCC{CondCode::E, j.target});
        },
        [&](const NirJumpIfNotZero &j) {
          const AsmType type = asm_type(type_of(j.condition));
          if (type == AsmType::Double) {
            instructions.push_back(FloatBinary{
                FloatBinary::Op::Xor, Reg{RegId::XMM14}, Reg{RegId::XMM14}});
            instructions.push_back(Mov{type, op(j.condition),
                                       Reg{RegId::XMM15}});
            instructions.push_back(Cmpl{type, Reg{RegId::XMM14},
                                         Reg{RegId::XMM15}});
            instructions.push_back(JmpCC{CondCode::P, j.target});
            instructions.push_back(JmpCC{CondCode::NE, j.target});
            return;
          }
          instructions.push_back(Cmpl{type, op(j.condition), Imm{0}});
          instructions.push_back(JmpCC{CondCode::NE, j.target});
        },
        [&](const NirJumpIfNotEqual &j) {
          const AsmType type = asm_type(type_of(j.value1));
          if (type == AsmType::Double) {
            instructions.push_back(Mov{type, op(j.value1),
                                       Reg{RegId::XMM15}});
            instructions.push_back(Cmpl{type, op(j.value2),
                                         Reg{RegId::XMM15}});
            instructions.push_back(JmpCC{CondCode::NE, j.target});
            instructions.push_back(JmpCC{CondCode::P, j.target});
            return;
          }
          instructions.push_back(Cmpl{type, op(j.value2), op(j.value1)});
          instructions.push_back(JmpCC{CondCode::NE, j.target});
        },
        [&](const NirLabel &l) { instructions.push_back(AsmLabel{l.name}); },
        [&](const NirCall &c) { emit_function_call(c, instructions, constants); }
    }, instr);
  }
  return {func.name, func.global, func.params, std::move(instructions)};
}

static Operand fix_operand(Operand o, std::unordered_map<std::string, int> &offsets, int &next_offset) {
  if (auto *p = std::get_if<Pseudo>(&o)) {
    if (auto it = offsets.find(p->name); it != offsets.end()) {
      return Stack{it->second};
    }
    if (const Symbol *symbol = symbol_table().find(p->name)) {
      if (std::holds_alternative<Symbol::StaticAttr>(symbol->attrs))
        return Data{p->name};
      const bool wide = symbol->type.kind == TypeKind::Long ||
                        symbol->type.kind == TypeKind::ULong ||
                        symbol->type.kind == TypeKind::Pointer ||
                        symbol->type.kind == TypeKind::Double;
      if (wide)
        next_offset = (next_offset - 8) & ~7;
      else
        next_offset -= 4;
    } else {
      next_offset -= 4;
    }
    return offsets[p->name] = next_offset, Stack{next_offset};
  }
  return o;
}

static std::pair<AsmFunction, int> replace_pseudos(const AsmFunction &func) {
  std::unordered_map<std::string, int> offsets;
  int next_offset = 0;

  auto fix = [&](Operand o) { return fix_operand(o, offsets, next_offset); };

  std::vector<AsmInstruction> instructions;
  for (const auto &instr : func.instructions) {
    instructions.push_back(std::visit([&](auto i) -> AsmInstruction {
      using T = std::decay_t<decltype(i)>;
      if constexpr (requires { i.src; i.dst; }) {
        if constexpr (requires { i.op; })
          return T{i.op, fix(i.src), fix(i.dst)};
        else if constexpr (requires { i.type; })
          return T{i.type, fix(i.src), fix(i.dst)};
        else
          return T{fix(i.src), fix(i.dst)};
      } else if constexpr (requires { i.operand; }) {
        if constexpr (requires { i.cc; }) return T{i.cc, fix(i.operand)};
        else if constexpr (requires { i.op; i.type; })
          return T{i.op, i.type, fix(i.operand)};
        else if constexpr (requires { i.op; }) return T{i.op, fix(i.operand)};
        else if constexpr (requires { i.type; })
          return T{i.type, fix(i.operand)};
        else return T{fix(i.operand)};
      } else {
        return i;
      }
    }, instr));
  }
  return {{func.name, func.global, func.params, std::move(instructions)},
          -next_offset};
}

static AsmFunction fix_up(const AsmFunction &func, int stack_bytes) {
  std::vector<AsmInstruction> fixed;

  auto fix_mov = [&](const Mov &mov) {
    if (mov.type == AsmType::Double) {
      if (is_memory(mov.src) && is_memory(mov.dst)) {
        fixed.push_back(Mov{mov.type, mov.src, Reg{RegId::XMM14}});
        fixed.push_back(Mov{mov.type, Reg{RegId::XMM14}, mov.dst});
      } else {
        fixed.push_back(mov);
      }
      return;
    }
    const bool large_quadword_immediate =
        mov.type == AsmType::Quadword &&
        std::holds_alternative<Imm>(mov.src) && is_memory(mov.dst) &&
        (std::get<Imm>(mov.src).value < INT32_MIN ||
         std::get<Imm>(mov.src).value > INT32_MAX);
    if (large_quadword_immediate) {
      fixed.push_back(Mov{mov.type, mov.src, Reg{RegId::R10}});
      fixed.push_back(Mov{mov.type, Reg{RegId::R10}, mov.dst});
    } else if (is_memory(mov.src) && is_memory(mov.dst)) {
      const RegId scratch = std::holds_alternative<Indirect>(mov.dst)
                                ? RegId::R11
                                : RegId::R10;
      fixed.push_back(Mov{mov.type, mov.src, Reg{scratch}});
      fixed.push_back(Mov{mov.type, Reg{scratch}, mov.dst});
    } else {
      fixed.push_back(mov);
    }
  };

  auto fix_bin = [&](const auto &inst) {
    using T = std::decay_t<decltype(inst)>;
    if (is_memory(inst.src) && is_memory(inst.dst)) {
      fixed.push_back(Mov{inst.type, inst.src, Reg{RegId::R10}});
      fixed.push_back(T{inst.type, Reg{RegId::R10}, inst.dst});
    } else {
      fixed.push_back(inst);
    }
  };

  for (const auto &instr : func.instructions) {
    std::visit(Overload{
      [&](const Mov &m) { fix_mov(m); },
      [&](const Movsx &m) {
        if (std::holds_alternative<Imm>(m.src)) {
          fix_mov(Mov{AsmType::Quadword, m.src, m.dst});
        } else if (is_memory(m.dst)) {
          fixed.push_back(Movsx{m.src, Reg{RegId::R11}});
          fixed.push_back(
              Mov{AsmType::Quadword, Reg{RegId::R11}, m.dst});
        } else {
          fixed.push_back(m);
        }
      },
      [&](const Movzx &m) {
        if (is_memory(m.dst)) {
          fixed.push_back(Mov{AsmType::Longword, m.src, Reg{RegId::R11}});
          fixed.push_back(Mov{AsmType::Quadword, Reg{RegId::R11}, m.dst});
        } else {
          fixed.push_back(Mov{AsmType::Longword, m.src, m.dst});
        }
      },
      [&](const Lea &l) {
        if (is_memory(l.dst)) {
          fixed.push_back(Lea{l.src, Reg{RegId::R11}});
          fixed.push_back(
              Mov{AsmType::Quadword, Reg{RegId::R11}, l.dst});
        } else {
          fixed.push_back(l);
        }
      },
      [&](const Div &d) {
        if (std::holds_alternative<Imm>(d.operand)) {
          fixed.push_back(Mov{d.type, d.operand, Reg{RegId::R10}});
          fixed.push_back(Div{d.type, Reg{RegId::R10}});
        } else {
          fixed.push_back(d);
        }
      },
      [&](const FloatBinary &i) {
        if (std::holds_alternative<Reg>(i.dst) &&
            static_cast<uint8_t>(std::get<Reg>(i.dst).id) >=
                static_cast<uint8_t>(RegId::XMM0)) {
          fixed.push_back(i);
        } else {
          fixed.push_back(Mov{AsmType::Double, i.dst, Reg{RegId::XMM15}});
          fixed.push_back(
              FloatBinary{i.op, i.src, Reg{RegId::XMM15}});
          fixed.push_back(
              Mov{AsmType::Double, Reg{RegId::XMM15}, i.dst});
        }
      },
      [&](const Cvtsi2sd &i) {
        Operand src = i.src;
        if (std::holds_alternative<Imm>(src)) {
          fixed.push_back(Mov{i.type, src, Reg{RegId::R10}});
          src = Reg{RegId::R10};
        }
        if (is_memory(i.dst)) {
          fixed.push_back(Cvtsi2sd{i.type, src, Reg{RegId::XMM15}});
          fixed.push_back(Mov{AsmType::Double, Reg{RegId::XMM15}, i.dst});
        } else {
          fixed.push_back(Cvtsi2sd{i.type, src, i.dst});
        }
      },
      [&](const Cvttsd2si &i) {
        if (is_memory(i.dst)) {
          fixed.push_back(Cvttsd2si{i.type, i.src, Reg{RegId::R11}});
          fixed.push_back(Mov{i.type, Reg{RegId::R11}, i.dst});
        } else {
          fixed.push_back(i);
        }
      },
      [&](const Addl &a) { fix_bin(a); },
        [&](const Subl &s) { fix_bin(s); },
        [&](const Andl &a) { fix_bin(a); },
        [&](const Orl &o) { fix_bin(o); },
        [&](const Xorl &x) { fix_bin(x); },
        [&](const Imull &m) {
          if (is_memory(m.dst)) {
            fixed.push_back(Mov{m.type, m.src, Reg{RegId::AX}});
            fixed.push_back(Mov{m.type, m.dst, Reg{RegId::R10}});
            fixed.push_back(Imull{m.type, Reg{RegId::AX}, Reg{RegId::R10}});
            fixed.push_back(Mov{m.type, Reg{RegId::R10}, m.dst});
          } else {
            fixed.push_back(m);
          }
        },
        [&](const Cmpl &c) {
          if (c.type == AsmType::Double) {
            Operand dst = c.dst;
            if (!std::holds_alternative<Reg>(dst) ||
                static_cast<uint8_t>(std::get<Reg>(dst).id) <
                    static_cast<uint8_t>(RegId::XMM0)) {
              fixed.push_back(Mov{AsmType::Double, dst, Reg{RegId::XMM15}});
              dst = Reg{RegId::XMM15};
            }
            fixed.push_back(Cmpl{c.type, c.src, dst});
            return;
          }
          Operand src = c.src;
          Operand dst = c.dst;
          if (std::holds_alternative<Imm>(dst)) {
            fix_mov(Mov{c.type, dst, Reg{RegId::R11}});
            dst = Reg{RegId::R11};
          }
          if (is_memory(src) && is_memory(dst)) {
            fix_mov(Mov{c.type, src, Reg{RegId::R10}});
            src = Reg{RegId::R10};
          } else if (c.type == AsmType::Quadword &&
                     std::holds_alternative<Imm>(src) &&
                     (std::get<Imm>(src).value < INT32_MIN ||
                      std::get<Imm>(src).value > INT32_MAX)) {
            fix_mov(Mov{c.type, src, Reg{RegId::R10}});
            src = Reg{RegId::R10};
          }
          fixed.push_back(Cmpl{c.type, src, dst});
        },
        [&](const auto &i) { fixed.push_back(i); }
    }, instr);
  }

  const int rounded_bytes = (stack_bytes + 15) & ~15;
  std::vector<AsmInstruction> result;
  if (rounded_bytes > 0) result.push_back(AllocateStack{rounded_bytes});
  std::move(fixed.begin(), fixed.end(), std::back_inserter(result));

  return {func.name, func.global, func.params, std::move(result)};
}

static AsmProgram nir_to_asm(const NirProgram &program) {
  std::vector<AsmTopLevel> top_levels;
  ConstantPool constants;
  top_levels.reserve(program.top_levels.size());

  for (const auto &definition : program.top_levels) {
    std::visit(
        Overload{
            [&](const NirStaticVariable &variable) {
              top_levels.push_back(AsmStaticVariable{
                  variable.name, variable.global, variable.type,
                  variable.init});
            },
            [&](const NirFunction &func) {
              if (func.instructions.empty()) return;
              auto asm_func = nir_to_asm(func, constants);
              auto [pseudo_free_func, stack_bytes] =
                  replace_pseudos(asm_func);
              top_levels.push_back(fix_up(pseudo_free_func, stack_bytes));
            }},
        definition);
  }

  for (auto &constant : constants)
    top_levels.push_back(std::move(constant));
  return {std::move(top_levels)};
}

export AsmProgram codegen(const Program &program) {
  return nir_to_asm(emit_nir(program));
}

static std::string platform_prefix() {
#if defined(__APPLE__)
  return "_";
#else
  return "";
#endif
}

static std::string call_target(const std::string &name) {
  const std::string target = platform_prefix() + name;
#if defined(__linux__)
  const Symbol *symbol = symbol_table().find(name);
  const auto *function = symbol
                             ? std::get_if<Symbol::FunAttr>(&symbol->attrs)
                             : nullptr;
  if (!function || !function->defined)
    return target + "@PLT";
#endif
  return target;
}

static std::string reg_q_str(RegId id);

static std::string xmm_str(RegId id) {
  switch (id) {
    case RegId::XMM0: return "%xmm0";
    case RegId::XMM1: return "%xmm1";
    case RegId::XMM2: return "%xmm2";
    case RegId::XMM3: return "%xmm3";
    case RegId::XMM4: return "%xmm4";
    case RegId::XMM5: return "%xmm5";
    case RegId::XMM6: return "%xmm6";
    case RegId::XMM7: return "%xmm7";
    case RegId::XMM14: return "%xmm14";
    case RegId::XMM15: return "%xmm15";
    default: std::unreachable();
  }
}

static std::string reg_str(RegId id, AsmType type) {
  if (type == AsmType::Double ||
      static_cast<uint8_t>(id) >= static_cast<uint8_t>(RegId::XMM0))
    return xmm_str(id);
  if (type == AsmType::Quadword) return reg_q_str(id);
  switch (id) {
    case RegId::AX: return "%eax";
    case RegId::CX: return "%ecx";
    case RegId::DX: return "%edx";
    case RegId::DI: return "%edi";
    case RegId::SI: return "%esi";
    case RegId::R8: return "%r8d";
    case RegId::R9: return "%r9d";
    case RegId::R10: return "%r10d";
    case RegId::R11: return "%r11d";
  }
  std::unreachable();
}

static std::string operand_str(const Operand &o,
                               AsmType type = AsmType::Longword) {
  return std::visit(Overload{
      [](const Imm &i) { return "$" + std::to_string(i.value); },
      [&](const Reg &r) {
        return reg_str(r.id, type);
      },
      [](const Stack &s) { return std::to_string(s.offset) + "(%rbp)"; },
      [](const Indirect &i) { return "(" + reg_q_str(i.base) + ")"; },
      [](const Data &d) {
        if (!d.constant) return platform_prefix() + d.name + "(%rip)";
#if defined(__APPLE__)
        return d.name + "(%rip)";
#else
        return "." + d.name + "(%rip)";
#endif
      },
      [](const Pseudo &) -> std::string { std::unreachable(); }
    }, o);
}

static std::string reg_q_str(RegId id) {
  switch (id) {
    case RegId::AX: return std::string("%rax");
    case RegId::CX: return std::string("%rcx");
    case RegId::DX: return std::string("%rdx");
    case RegId::DI: return std::string("%rdi");
    case RegId::SI: return std::string("%rsi");
    case RegId::R8: return std::string("%r8");
    case RegId::R9: return std::string("%r9");
    case RegId::R10: return std::string("%r10");
    case RegId::R11: return std::string("%r11");
  }
  std::unreachable();
}

static std::string operand_q_str(const Operand &o) {
  return std::visit(Overload{
      [](const Imm &i) { return "$" + std::to_string(i.value); },
      [](const Reg &r) { return reg_q_str(r.id); },
      [](const auto &) -> std::string { std::unreachable(); },
  }, o);
}

static std::string operand_byte_str(const Operand &o) {
  return std::visit(Overload{
      [](const Imm &i) { return "$" + std::to_string(i.value); },
      [](const Reg &r) {
        switch (r.id) {
          case RegId::AX: return std::string("%al");
          case RegId::CX: return std::string("%cl");
          case RegId::DX: return std::string("%dl");
          case RegId::DI: return std::string("%dil");
          case RegId::SI: return std::string("%sil");
          case RegId::R8: return std::string("%r8b");
          case RegId::R9: return std::string("%r9b");
          case RegId::R10: return std::string("%r10b");
          case RegId::R11: return std::string("%r11b");
        }
        std::unreachable();
      },
      [](const Stack &s) { return std::to_string(s.offset) + "(%rbp)"; },
      [](const Data &d) { return platform_prefix() + d.name + "(%rip)"; },
      [](const Indirect &) -> std::string { std::unreachable(); },
      [](const Pseudo &) -> std::string { std::unreachable(); }
  }, o);
}

static std::string cond_code_str(CondCode cc) {
  switch (cc) {
    case CondCode::E: return "e";
    case CondCode::NE: return "ne";
    case CondCode::G: return "g";
    case CondCode::GE: return "ge";
    case CondCode::L: return "l";
    case CondCode::LE: return "le";
    case CondCode::A: return "a";
    case CondCode::AE: return "ae";
    case CondCode::B: return "b";
    case CondCode::BE: return "be";
    case CondCode::P: return "p";
    case CondCode::NP: return "np";
  }
  std::unreachable();
}

template <typename T>
static std::string inst_name() {
  std::string name(std::meta::identifier_of(^^T));
  for (char &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return name;
}

static std::string_view suffix(AsmType type) {
  switch (type) {
    case AsmType::Longword: return "l";
    case AsmType::Quadword: return "q";
    case AsmType::Double: return "sd";
  }
  std::unreachable();
}

export void emit_asm(const AsmProgram &program, std::string &output) {
  auto prefix = platform_prefix();

  for (const auto &top_level : program.top_levels) {
    std::visit(
        Overload{
            [&](const AsmFunction &func) {
              if (func.global)
                output += "    .globl " + prefix + func.name + "\n";
              output += "    .text\n";
              output += prefix + func.name + ":\n";
              output += "    pushq %rbp\n";
              output += "    movq %rsp, %rbp\n";

              for (const auto &instr : func.instructions) {
                std::visit(Overload{
                               [&](const Mov &m) {
                                 const bool needs_movabs =
                                     m.type == AsmType::Quadword &&
                                     std::holds_alternative<Imm>(m.src) &&
                                     std::holds_alternative<Reg>(m.dst) &&
                                     (std::get<Imm>(m.src).value < INT32_MIN ||
                                      std::get<Imm>(m.src).value > INT32_MAX);
                                 output += needs_movabs
                                               ? "    movabsq "
                                               : "    mov" +
                                                     std::string(suffix(m.type)) +
                                                     " ";
                                 output +=
                                           operand_str(m.src, m.type) + ", " +
                                           operand_str(m.dst, m.type) + "\n";
                               },
                               [&](const Movsx &m) {
                                 output += "    movslq " +
                                           operand_str(m.src,
                                                       AsmType::Longword) +
                                           ", " +
                                           operand_str(m.dst,
                                                       AsmType::Quadword) +
                                           "\n";
                               },
                               [&](const Movzx &m) {
                                 output += "    movl " +
                                           operand_str(m.src,
                                                       AsmType::Longword) +
                                           ", %r11d\n";
                                 output += "    movq %r11, " +
                                           operand_str(m.dst,
                                                       AsmType::Quadword) +
                                           "\n";
                               },
                               [&](const Lea &l) {
                                 output += "    leaq " +
                                           operand_str(l.src,
                                                       AsmType::Quadword) +
                                           ", " +
                                           operand_str(l.dst,
                                                       AsmType::Quadword) +
                                           "\n";
                               },
                               [&](const FloatBinary &i) {
                                 const char *name = nullptr;
                                 switch (i.op) {
                                   case FloatBinary::Op::Add: name = "addsd"; break;
                                   case FloatBinary::Op::Sub: name = "subsd"; break;
                                   case FloatBinary::Op::Mult: name = "mulsd"; break;
                                   case FloatBinary::Op::Div: name = "divsd"; break;
                                   case FloatBinary::Op::Xor: name = "xorpd"; break;
                                 }
                                 output += "    " + std::string(name) + " " +
                                           operand_str(i.src, AsmType::Double) +
                                           ", " +
                                           operand_str(i.dst, AsmType::Double) +
                                           "\n";
                               },
                               [&](const Cvtsi2sd &i) {
                                 output += "    cvtsi2sd" +
                                           std::string(suffix(i.type)) + " " +
                                           operand_str(i.src, i.type) + ", " +
                                           operand_str(i.dst, AsmType::Double) +
                                           "\n";
                               },
                               [&](const Cvttsd2si &i) {
                                 output += "    cvttsd2si" +
                                           std::string(suffix(i.type)) + " " +
                                           operand_str(i.src, AsmType::Double) +
                                           ", " +
                                           operand_str(i.dst, i.type) + "\n";
                               },
                               [&](const AsmUnary &u) {
                                 output += "    " +
                                           std::string(u.op == AsmUnaryOp::Neg
                                                           ? "neg"
                                                           : "not") +
                                           std::string(suffix(u.type)) + " " +
                                           operand_str(u.operand, u.type) +
                                           "\n";
                               },
                               [&](const Addl &i) {
                                 output += "    add" +
                                           std::string(suffix(i.type)) + " " +
                                           operand_str(i.src, i.type) + ", " +
                                           operand_str(i.dst, i.type) + "\n";
                               },
                               [&](const Subl &i) {
                                 output += "    sub" +
                                           std::string(suffix(i.type)) + " " +
                                           operand_str(i.src, i.type) + ", " +
                                           operand_str(i.dst, i.type) + "\n";
                               },
                               [&](const Imull &i) {
                                 output += "    imul" +
                                           std::string(suffix(i.type)) + " " +
                                           operand_str(i.src, i.type) + ", " +
                                           operand_str(i.dst, i.type) + "\n";
                               },
                               [&](const Andl &i) {
                                 output += "    and" +
                                           std::string(suffix(i.type)) + " " +
                                           operand_str(i.src, i.type) + ", " +
                                           operand_str(i.dst, i.type) + "\n";
                               },
                               [&](const Orl &i) {
                                 output += "    or" +
                                           std::string(suffix(i.type)) + " " +
                                           operand_str(i.src, i.type) + ", " +
                                           operand_str(i.dst, i.type) + "\n";
                               },
                               [&](const Xorl &i) {
                                 output += "    xor" +
                                           std::string(suffix(i.type)) + " " +
                                           operand_str(i.src, i.type) + ", " +
                                           operand_str(i.dst, i.type) + "\n";
                               },
                               [&](const Idivl &d) {
                                 output += "    idiv" +
                                           std::string(suffix(d.type)) + " " +
                                           operand_str(d.operand, d.type) +
                                           "\n";
                               },
                               [&](const Div &d) {
                                 output += "    div" +
                                           std::string(suffix(d.type)) + " " +
                                           operand_str(d.operand, d.type) +
                                           "\n";
                               },
                               [&](const Cdq &d) {
                                 output += d.type == AsmType::Longword
                                               ? "    cdq\n"
                                               : "    cqo\n";
                               },
                               [&](const Cmpl &c) {
                                 output += c.type == AsmType::Double
                                               ? "    comisd "
                                               : "    cmp" +
                                                     std::string(suffix(c.type)) +
                                                     " ";
                                 output +=
                                           operand_str(c.src, c.type) + ", " +
                                           operand_str(c.dst, c.type) + "\n";
                               },
                               [&](const Shll &s) {
                                 output += "    sh" +
                                           std::string(s.type == AsmType::Longword
                                                           ? "ll"
                                                           : "lq") +
                                           " %cl, " +
                                           operand_str(s.operand, s.type) +
                                           "\n";
                               },
                               [&](const Sarl &s) {
                                 output += "    sar" +
                                           std::string(s.type == AsmType::Longword
                                                           ? "l"
                                                           : "q") +
                                           " %cl, " +
                                           operand_str(s.operand, s.type) +
                                           "\n";
                               },
                               [&](const Shrl &s) {
                                 output += "    shr" +
                                           std::string(s.type == AsmType::Longword
                                                           ? "l"
                                                           : "q") +
                                           " %cl, " +
                                           operand_str(s.operand, s.type) +
                                           "\n";
                               },
                               [&](const Jmp &j) {
                                 output += "    jmp " + j.target + "\n";
                               },
                               [&](const JmpCC &j) {
                                 output += "    j" + cond_code_str(j.cc) +
                                           " " + j.target + "\n";
                               },
                               [&](const SetCC &s) {
                                 output += "    set" + cond_code_str(s.cc) +
                                           " " + operand_byte_str(s.operand) +
                                           "\n";
                               },
                               [&](const AsmLabel &l) {
                                 output += l.name + ":\n";
                               },
                               [&](const AllocateStack &a) {
                                 output += "    subq $" +
                                           std::to_string(a.bytes) +
                                           ", %rsp\n";
                               },
                               [&](const DeallocateStack &a) {
                                 output += "    addq $" +
                                           std::to_string(a.bytes) +
                                           ", %rsp\n";
                               },
                               [&](const Pushq &p) {
                                 if (const auto *imm =
                                         std::get_if<Imm>(&p.operand);
                                     imm && (imm->value < INT32_MIN ||
                                             imm->value > INT32_MAX)) {
                                   output += "    movabsq " +
                                             operand_str(p.operand,
                                                         AsmType::Quadword) +
                                             ", %r10\n";
                                   output += "    pushq %r10\n";
                                 } else {
                                   output += "    pushq " +
                                             operand_q_str(p.operand) + "\n";
                                 }
                               },
                               [&](const PushDouble &p) {
                                 output += "    movsd " +
                                           operand_str(p.operand,
                                                       AsmType::Double) +
                                           ", %xmm14\n";
                                 output += "    movq %xmm14, %r10\n";
                                 output += "    pushq %r10\n";
                               },
                               [&](const Call &c) {
                                 output += "    call " + call_target(c.name) +
                                           "\n";
                               },
                               [&](const Ret &) {
                                 output +=
                                     "    movq %rbp, %rsp\n    popq %rbp\n"
                                     "    ret\n";
                               },
                               [&](const auto &i) {
                                 using T = std::decay_t<decltype(i)>;
                                 if constexpr (requires { i.src; i.dst; }) {
                                   output += "    " + inst_name<T>() + " " +
                                             operand_str(i.src) + ", " +
                                             operand_str(i.dst) + "\n";
                                 }
                               }},
                           instr);
              }
            },
            [&](const AsmStaticVariable &variable) {
              const std::string name = prefix + variable.name;
              const bool zero = variable.type.kind != TypeKind::Double &&
                  std::visit([](const auto &init) { return init.value == 0; },
                             variable.init);
              const bool is_long = variable.type.kind == TypeKind::Long ||
                                   variable.type.kind == TypeKind::ULong ||
                                   variable.type.kind == TypeKind::Pointer ||
                                   variable.type.kind == TypeKind::Double;
              if (variable.global) output += "    .globl " + name + "\n";
              output += zero ? "    .bss\n" : "    .data\n";
              const int alignment = is_long ? 8 : 4;
#if defined(__linux__)
              output += "    .align " + std::to_string(alignment) + "\n";
#else
              output += "    .balign " + std::to_string(alignment) + "\n";
#endif
              output += name + ":\n";
              if (zero)
                output += is_long ? "    .zero 8\n" : "    .zero 4\n";
              else {
                const std::string value = std::visit(
                    Overload{
                        [](const IntInit &init) {
                          return std::to_string(init.value);
                        },
                        [](const LongInit &init) {
                          return std::to_string(init.value);
                        },
                        [](const UIntInit &init) {
                          return std::to_string(init.value);
                        },
                        [](const ULongInit &init) {
                          return std::to_string(init.value);
                        },
                        [](const DoubleInit &init) {
                          return std::to_string(
                              std::bit_cast<uint64_t>(init.value));
                        }},
                    variable.init);
                output += (is_long ? "    .quad " : "    .long ") + value +
                          "\n";
              }
            },
            [&](const AsmStaticConstant &constant) {
              const std::string name = constant.name;
#if defined(__APPLE__)
              output += constant.alignment == 16 ? "    .literal16\n"
                                                 : "    .literal8\n";
              output += "    .balign " +
                        std::to_string(constant.alignment) + "\n";
#else
              output += "    .section .rodata\n";
              output += "    .align " + std::to_string(constant.alignment) +
                        "\n";
#endif
#if defined(__APPLE__)
              output += name + ":\n";
#else
              output += "." + name + ":\n";
#endif
              output += "    .quad " +
                        std::to_string(std::bit_cast<uint64_t>(
                            constant.init.value)) +
                        "\n";
#if defined(__APPLE__)
              if (constant.alignment == 16) output += "    .quad 0\n";
#endif
            }},
        top_level);
  }

#if defined(__linux__)
  output += "\n    .section .note.GNU-stack,\"\",@progbits\n";
#endif
}
