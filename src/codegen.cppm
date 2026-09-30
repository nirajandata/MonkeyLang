module;

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
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
  enum class RegId : uint8_t { AX, CX, DX, DI, SI, R8, R9, R10, R11 };

  struct Reg { RegId id; };
  struct Imm { int32_t value; };
  struct Pseudo { std::string name; };
  struct Stack { int offset; };

  using Operand = std::variant<Imm, Reg, Pseudo, Stack>;

  struct Mov { Operand src; Operand dst; };
  struct Addl { Operand src; Operand dst; };
  struct Subl { Operand src; Operand dst; };
  struct Imull { Operand src; Operand dst; };
  struct Idivl { Operand operand; };
  struct Cdq {};
  struct Cmpl { Operand src; Operand dst; };
  struct Andl { Operand src; Operand dst; };
  struct Orl { Operand src; Operand dst; };
  struct Xorl { Operand src; Operand dst; };
  struct Shll { Operand operand; };
  struct Sarl { Operand operand; };

  enum class AsmUnaryOp : uint8_t { Neg, Not };

  struct AsmUnary {
    AsmUnaryOp op;
    Operand operand;
  };

  enum class CondCode : uint8_t { E, NE, G, GE, L, LE };

  struct Jmp { std::string target; };
  struct JmpCC { CondCode cc; std::string target; };
  struct SetCC { CondCode cc; Operand operand; };
  struct AsmLabel { std::string name; };
  struct AllocateStack { int bytes; };
  struct DeallocateStack { int bytes; };
  struct Pushq { Operand operand; };
  struct Call { std::string name; };
  struct Ret {};

  using AsmInstruction =
      std::variant<Mov, AsmUnary, Addl, Subl, Imull, Idivl, Cdq, Cmpl, Andl,
                   Orl, Xorl, Shll, Sarl, Jmp, JmpCC, SetCC, AsmLabel,
                   AllocateStack, DeallocateStack, Pushq, Call, Ret>;

  struct AsmFunction {
    std::string name;
    std::vector<std::string> params;
    std::vector<AsmInstruction> instructions;
  };

  struct AsmProgram {
    std::vector<AsmFunction> functions;
  };
}

static Operand nir_val_to_operand(const NirVal &val) {
  return std::visit(Overload{
    [](const NirConstant &c) -> Operand { return Imm{c.value}; },
    [](const NirVar &v) -> Operand { return Pseudo{v.name}; }
  }, val);
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
  return std::holds_alternative<Stack>(o);
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

static CondCode convert_relational(NirBinaryOp op) {
  switch (op) {
    case NirBinaryOp::Equal: return CondCode::E;
    case NirBinaryOp::NotEqual: return CondCode::NE;
    case NirBinaryOp::LessThan: return CondCode::L;
    case NirBinaryOp::LessOrEqual: return CondCode::LE;
    case NirBinaryOp::GreaterThan: return CondCode::G;
    case NirBinaryOp::GreaterOrEqual: return CondCode::GE;
    default: std::unreachable();
  }
}

static void emit_function_call(const NirCall &c,
                               std::vector<AsmInstruction> &instructions) {
  std::vector<Operand> args;
  args.reserve(c.args.size());
  for (const auto &arg : c.args) args.push_back(nir_val_to_operand(arg));

  const size_t num_stack_args =
      args.size() > num_arg_registers ? args.size() - num_arg_registers : 0;
  const int stack_padding = num_stack_args % 2 != 0 ? 8 : 0;
  if (stack_padding != 0)
    instructions.push_back(AllocateStack{stack_padding});

  for (size_t i = 0; i < args.size() && i < num_arg_registers; ++i)
    instructions.push_back(Mov{args[i], Reg{arg_registers[i]}});

  for (size_t i = args.size(); i > num_arg_registers; --i) {
    const Operand &arg = args[i - 1];
    if (std::holds_alternative<Reg>(arg) || std::holds_alternative<Imm>(arg)) {
      instructions.push_back(Pushq{arg});
    } else {
      instructions.push_back(Mov{arg, Reg{RegId::AX}});
      instructions.push_back(Pushq{Reg{RegId::AX}});
    }
  }

  instructions.push_back(Call{c.name});

  const int bytes_to_remove =
      static_cast<int>(8 * num_stack_args) + stack_padding;
  if (bytes_to_remove != 0)
    instructions.push_back(DeallocateStack{bytes_to_remove});

  instructions.push_back(Mov{Reg{RegId::AX}, nir_val_to_operand(c.dst)});
}

static AsmFunction nir_to_asm(const NirFunction &func) {
  std::vector<AsmInstruction> instructions;
  auto op = nir_val_to_operand;

  for (size_t i = 0; i < func.params.size(); ++i)
    instructions.push_back(Mov{param_source(i), Pseudo{func.params[i]}});

  for (const auto &instr : func.instructions) {
    std::visit(Overload{
        [&](const NirReturn &r) {
          instructions.push_back(Mov{op(r.val), Reg{RegId::AX}});
          instructions.push_back(Ret{});
        },
        [&](const NirUnary &u) {
          if (u.op == NirUnaryOp::Not) {
            instructions.push_back(Mov{op(u.src), Reg{RegId::R10}});
            instructions.push_back(Cmpl{Imm{0}, Reg{RegId::R10}});
            instructions.push_back(Mov{Imm{0}, Reg{RegId::R11}});
            instructions.push_back(SetCC{CondCode::E, Reg{RegId::R11}});
            instructions.push_back(Mov{Reg{RegId::R11}, op(u.dst)});
          } else {
            instructions.push_back(Mov{op(u.src), op(u.dst)});
            instructions.push_back(AsmUnary{u.op == NirUnaryOp::Negate ? AsmUnaryOp::Neg : AsmUnaryOp::Not, op(u.dst)});
          }
        },
        [&](const NirBinary &b) {
          auto src1 = op(b.src1);
          auto src2 = op(b.src2);
          auto dst = op(b.dst);

          if (is_relational(b.op)) {
            instructions.push_back(Mov{src1, Reg{RegId::R10}});
            instructions.push_back(Cmpl{src2, Reg{RegId::R10}});
            instructions.push_back(Mov{Imm{0}, Reg{RegId::R11}});
            instructions.push_back(SetCC{convert_relational(b.op), Reg{RegId::R11}});
            instructions.push_back(Mov{Reg{RegId::R11}, dst});
            return;
          }

          auto ensure_r10 = [&](const Operand &s) {
            if (auto *r = std::get_if<Reg>(&s); !r || r->id != RegId::R10) {
              instructions.push_back(Mov{s, Reg{RegId::R10}});
            }
          };

          switch (b.op) {
            case NirBinaryOp::Add: instructions.push_back(Mov{src1, dst}); ensure_r10(src2); instructions.push_back(Addl{Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::BitwiseAnd: instructions.push_back(Mov{src1, dst}); ensure_r10(src2); instructions.push_back(Andl{Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::BitwiseOr: instructions.push_back(Mov{src1, dst}); ensure_r10(src2); instructions.push_back(Orl{Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::BitwiseXor: instructions.push_back(Mov{src1, dst}); ensure_r10(src2); instructions.push_back(Xorl{Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::Subtract: instructions.push_back(Mov{src1, dst}); ensure_r10(src2); instructions.push_back(Subl{Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::Multiply: instructions.push_back(Mov{src1, dst}); ensure_r10(src2); instructions.push_back(Imull{Reg{RegId::R10}, dst}); break;
            case NirBinaryOp::Divide:
            case NirBinaryOp::Remainder:
              instructions.push_back(Mov{src1, Reg{RegId::AX}});
              instructions.push_back(Cdq{});
              ensure_r10(src2);
              instructions.push_back(Idivl{Reg{RegId::R10}});
              instructions.push_back(Mov{b.op == NirBinaryOp::Divide ? Reg{RegId::AX} : Reg{RegId::DX}, dst});
              break;
            case NirBinaryOp::ShiftLeft:
            case NirBinaryOp::ShiftRight:
              instructions.push_back(Mov{src2, Reg{RegId::CX}});
              instructions.push_back(Mov{src1, dst});
              if (b.op == NirBinaryOp::ShiftLeft) instructions.push_back(Shll{dst});
              else instructions.push_back(Sarl{dst});
              break;
            default: break;
          }
        },
        [&](const NirCopy &c) { instructions.push_back(Mov{op(c.src), op(c.dst)}); },
        [&](const NirJump &j) { instructions.push_back(Jmp{j.target}); },
        [&](const NirJumpIfZero &j) {
          instructions.push_back(Cmpl{op(j.condition), Imm{0}});
          instructions.push_back(JmpCC{CondCode::E, j.target});
        },
        [&](const NirJumpIfNotZero &j) {
          instructions.push_back(Cmpl{op(j.condition), Imm{0}});
          instructions.push_back(JmpCC{CondCode::NE, j.target});
        },
        [&](const NirJumpIfNotEqual &j) {
          instructions.push_back(Cmpl{op(j.value2), op(j.value1)});
          instructions.push_back(JmpCC{CondCode::NE, j.target});
        },
        [&](const NirLabel &l) { instructions.push_back(AsmLabel{l.name}); },
        [&](const NirCall &c) { emit_function_call(c, instructions); }
    }, instr);
  }
  return {func.name, func.params, std::move(instructions)};
}

static Operand fix_operand(Operand o, std::unordered_map<std::string, int> &offsets, int &next_offset) {
  if (auto *p = std::get_if<Pseudo>(&o)) {
    if (auto it = offsets.find(p->name); it != offsets.end()) {
      return Stack{it->second};
    }
    next_offset -= 4;
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
        return T{fix(i.src), fix(i.dst)};
      } else if constexpr (requires { i.operand; }) {
        if constexpr (requires { i.cc; }) return T{i.cc, fix(i.operand)};
        else if constexpr (requires { i.op; }) return T{i.op, fix(i.operand)};
        else return T{fix(i.operand)};
      } else {
        return i;
      }
    }, instr));
  }
  return {{func.name, func.params, std::move(instructions)}, -next_offset};
}

static AsmFunction fix_up(const AsmFunction &func, int stack_bytes) {
  std::vector<AsmInstruction> fixed;

  auto fix_bin = [&](const auto &inst) {
    using T = std::decay_t<decltype(inst)>;
    if (is_memory(inst.src) && is_memory(inst.dst)) {
      fixed.push_back(Mov{inst.src, Reg{RegId::R10}});
      fixed.push_back(T{Reg{RegId::R10}, inst.dst});
    } else {
      fixed.push_back(inst);
    }
  };

  for (const auto &instr : func.instructions) {
    std::visit(Overload{
        [&](const Mov &m) { fix_bin(m); },
        [&](const Addl &a) { fix_bin(a); },
        [&](const Subl &s) { fix_bin(s); },
        [&](const Andl &a) { fix_bin(a); },
        [&](const Orl &o) { fix_bin(o); },
        [&](const Xorl &x) { fix_bin(x); },
        [&](const Imull &m) {
          if (is_memory(m.dst)) {
            fixed.push_back(Mov{m.src, Reg{RegId::AX}});
            fixed.push_back(Mov{m.dst, Reg{RegId::R10}});
            fixed.push_back(Imull{Reg{RegId::AX}, Reg{RegId::R10}});
            fixed.push_back(Mov{Reg{RegId::R10}, m.dst});
          } else {
            fixed.push_back(m);
          }
        },
        [&](const Cmpl &c) {
          if (is_memory(c.src) && is_memory(c.dst)) {
            fixed.push_back(Mov{c.src, Reg{RegId::R10}});
            fixed.push_back(Cmpl{Reg{RegId::R10}, c.dst});
          } else if (std::holds_alternative<Imm>(c.dst)) {
            fixed.push_back(Mov{c.dst, Reg{RegId::R11}});
            fixed.push_back(Cmpl{c.src, Reg{RegId::R11}});
          } else {
            fixed.push_back(c);
          }
        },
        [&](const auto &i) { fixed.push_back(i); }
    }, instr);
  }

  const int rounded_bytes = (stack_bytes + 15) & ~15;
  std::vector<AsmInstruction> result;
  if (rounded_bytes > 0) result.push_back(AllocateStack{rounded_bytes});
  std::move(fixed.begin(), fixed.end(), std::back_inserter(result));

  return {func.name, func.params, std::move(result)};
}

static std::vector<AsmFunction> nir_to_asm(const NirProgram &program) {
  std::vector<AsmFunction> functions;
  functions.reserve(program.functions.size());

  for (const auto &func : program.functions) {
    if (func.instructions.empty())
      continue;
    auto asm_func = nir_to_asm(func);
    auto [pseudo_free_func, stack_bytes] = replace_pseudos(asm_func);
    functions.push_back(fix_up(pseudo_free_func, stack_bytes));
  }

  return functions;
}

export AsmProgram codegen(const Program &program) {
  return {nir_to_asm(emit_nir(program))};
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
  if (!symbol || !symbol->defined)
    return target + "@PLT";
#endif
  return target;
}

static std::string operand_str(const Operand &o) {
  return std::visit(Overload{
      [](const Imm &i) { return "$" + std::to_string(i.value); },
      [](const Reg &r) {
        switch (r.id) {
          case RegId::AX: return std::string("%eax");
          case RegId::CX: return std::string("%ecx");
          case RegId::DX: return std::string("%edx");
          case RegId::DI: return std::string("%edi");
          case RegId::SI: return std::string("%esi");
          case RegId::R8: return std::string("%r8d");
          case RegId::R9: return std::string("%r9d");
          case RegId::R10: return std::string("%r10d");
          case RegId::R11: return std::string("%r11d");
        }
        std::unreachable();
      },
      [](const Stack &s) { return std::to_string(s.offset) + "(%rbp)"; },
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
  }
  std::unreachable();
}

template <typename T>
static std::string inst_name() {
  std::string name(std::meta::identifier_of(^^T));
  for (char &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return name;
}

export void emit_asm(const AsmProgram &program, std::string &output) {
  auto prefix = platform_prefix();

  for (const auto &func : program.functions) {
    output += "    .text\n";
    output += "    .globl " + prefix + func.name + "\n";
    output += prefix + func.name + ":\n";
    output += "    pushq %rbp\n";
    output += "    movq %rsp, %rbp\n";

    for (const auto &instr : func.instructions) {
      std::visit(Overload{
        [&](const Mov &m) { output += "    movl " + operand_str(m.src) + ", " + operand_str(m.dst) + "\n"; },
        [&](const AsmUnary &u) { output += "    " + std::string(u.op == AsmUnaryOp::Neg ? "negl" : "notl") + " " + operand_str(u.operand) + "\n"; },
        [&](const Idivl &d) { output += "    idivl " + operand_str(d.operand) + "\n"; },
        [&](const Cdq &) { output += "    cdq\n"; },
        [&](const Shll &s) { output += "    shll %cl, " + operand_str(s.operand) + "\n"; },
        [&](const Sarl &s) { output += "    sarl %cl, " + operand_str(s.operand) + "\n"; },
        [&](const Jmp &j) { output += "    jmp " + j.target + "\n"; },
        [&](const JmpCC &j) { output += "    j" + cond_code_str(j.cc) + " " + j.target + "\n"; },
        [&](const SetCC &s) { output += "    set" + cond_code_str(s.cc) + " " + operand_byte_str(s.operand) + "\n"; },
        [&](const AsmLabel &l) { output += l.name + ":\n"; },
        [&](const AllocateStack &a) { output += "    subq $" + std::to_string(a.bytes) + ", %rsp\n"; },
        [&](const DeallocateStack &a) { output += "    addq $" + std::to_string(a.bytes) + ", %rsp\n"; },
        [&](const Pushq &p) { output += "    pushq " + operand_q_str(p.operand) + "\n"; },
        [&](const Call &c) { output += "    call " + call_target(c.name) + "\n"; },
        [&](const Ret &) { output += "    movq %rbp, %rsp\n    popq %rbp\n    ret\n"; },
        [&](const auto &i) {
          using T = std::decay_t<decltype(i)>;
          if constexpr (requires { i.src; i.dst; }) {
            output += "    " + inst_name<T>() + " " + operand_str(i.src) + ", " + operand_str(i.dst) + "\n";
          }
        }
    }, instr);
    }
  }

#if defined(__linux__)
  output += "\n    .section .note.GNU-stack,\"\",@progbits\n";
#endif
}
