module;

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

export module codegen;

import ast;

// ── TACKY IR ────────────────────────────────────────────────────────

export struct TackyConstant { int32_t value; };
export struct TackyVar      { std::string name; };
export using TackyVal = std::variant<TackyConstant, TackyVar>;

export enum class TackyUnaryOp : uint8_t { Complement, Negate, Not };
export enum class TackyBinaryOp : uint8_t {
    Add, Subtract, Multiply, Divide, Remainder,
    Equal, NotEqual, LessThan, LessOrEqual, GreaterThan, GreaterOrEqual
};

export struct TackyReturn   { TackyVal val; };
export struct TackyUnary    { TackyUnaryOp op; TackyVal src; TackyVal dst; };
export struct TackyBinary   { TackyBinaryOp op; TackyVal src1; TackyVal src2; TackyVal dst; };
export struct TackyCopy     { TackyVal src; TackyVal dst; };
export struct TackyJump     { std::string target; };
export struct TackyJumpIfZero    { TackyVal condition; std::string target; };
export struct TackyJumpIfNotZero { TackyVal condition; std::string target; };
export struct TackyLabel    { std::string name; };

export using TackyInstruction = std::variant<TackyReturn, TackyUnary, TackyBinary,
                                              TackyCopy, TackyJump, TackyJumpIfZero,
                                              TackyJumpIfNotZero, TackyLabel>;

export struct TackyFunction { std::string name; std::vector<TackyInstruction> instructions; };
export struct TackyProgram  { TackyFunction function; };

// ── TACKY generation (AST -> TACKY) ─────────────────────────────────

static int temp_counter = 0;
static int label_counter = 0;

static std::string make_temporary() {
    return "tmp." + std::to_string(temp_counter++);
}

static std::string make_label(std::string_view prefix) {
    return std::string(prefix) + "." + std::to_string(label_counter++);
}

static TackyUnaryOp convert_ast_unop(const UnaryOp& op) {
    if (std::holds_alternative<Complement>(op)) return TackyUnaryOp::Complement;
    if (std::holds_alternative<Negate>(op))     return TackyUnaryOp::Negate;
    return TackyUnaryOp::Not;
}

static TackyBinaryOp convert_ast_binop(const BinaryOp& op) {
    if (std::holds_alternative<Add>(op))              return TackyBinaryOp::Add;
    if (std::holds_alternative<Subtract>(op))         return TackyBinaryOp::Subtract;
    if (std::holds_alternative<Multiply>(op))         return TackyBinaryOp::Multiply;
    if (std::holds_alternative<Divide>(op))           return TackyBinaryOp::Divide;
    if (std::holds_alternative<Remainder>(op))        return TackyBinaryOp::Remainder;
    if (std::holds_alternative<Equal>(op))            return TackyBinaryOp::Equal;
    if (std::holds_alternative<NotEqual>(op))         return TackyBinaryOp::NotEqual;
    if (std::holds_alternative<LessThan>(op))         return TackyBinaryOp::LessThan;
    if (std::holds_alternative<LessOrEqual>(op))      return TackyBinaryOp::LessOrEqual;
    if (std::holds_alternative<GreaterThan>(op))      return TackyBinaryOp::GreaterThan;
    return TackyBinaryOp::GreaterOrEqual;
}

static TackyVal emit_tacky_val(const Exp& exp,
                                std::vector<TackyInstruction>& instructions) {
    return std::visit(Overload{
        [](const Constant& c) -> TackyVal {
            return TackyConstant{c.value};
        },
        [&](const Unary& u) -> TackyVal {
            auto src = emit_tacky_val(*u.exp, instructions);
            TackyVal dst = TackyVar{make_temporary()};
            instructions.push_back(TackyUnary{convert_ast_unop(u.op), src, dst});
            return dst;
        },
        [&](const Binary& b) -> TackyVal {
            bool is_and = std::holds_alternative<And>(b.op);
            bool is_or  = std::holds_alternative<Or>(b.op);

            if (!is_and && !is_or) {
                auto left  = emit_tacky_val(*b.left, instructions);
                auto right = emit_tacky_val(*b.right, instructions);
                TackyVal dst = TackyVar{make_temporary()};
                instructions.push_back(TackyBinary{convert_ast_binop(b.op), left, right, dst});
                return dst;
            }

            TackyVal dst = TackyVar{make_temporary()};
            auto left = emit_tacky_val(*b.left, instructions);
            auto false_label = make_label(is_and ? "and_false" : "or_false");
            auto end_label   = make_label(is_and ? "and_end"   : "or_end");

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
    }, exp.value);
}

static TackyFunction emit_tacky_function(const Function& func) {
    std::vector<TackyInstruction> instructions;
    std::visit(Overload{
        [&](const Return& r) {
            instructions.push_back(TackyReturn{emit_tacky_val(r.value, instructions)});
        },
    }, func.body);
    return {std::string(func.name), std::move(instructions)};
}

export TackyProgram emit_tacky(const Program& program) {
    temp_counter = 0;
    label_counter = 0;
    return {emit_tacky_function(program.function)};
}

// ── Assembly AST ────────────────────────────────────────────────────

export enum class RegId : uint8_t { AX, DX, R10, R11 };

export struct Reg    { RegId id; };
export struct Imm    { int32_t value; };
export struct Pseudo { std::string name; };
export struct Stack  { int offset; };

export using Operand = std::variant<Imm, Reg, Pseudo, Stack>;

export struct Mov      { Operand src; Operand dst; };
export struct Addl     { Operand src; Operand dst; };
export struct Subl     { Operand src; Operand dst; };
export struct Imull    { Operand src; Operand dst; };
export struct Idivl    { Operand operand; };
export struct Cdq      {};
export struct Cmpl     { Operand src; Operand dst; };

export enum class AsmUnaryOp : uint8_t { Neg, Not };

export struct AsmUnary { AsmUnaryOp op; Operand operand; };
export enum class CondCode : uint8_t { E, NE, G, GE, L, LE };
export struct Jmp       { std::string target; };
export struct JmpCC     { CondCode cc; std::string target; };
export struct SetCC     { CondCode cc; Operand operand; };
export struct AsmLabel  { std::string name; };
export struct AllocateStack { int bytes; };
export struct Ret {};

export using AsmInstruction = std::variant<Mov, AsmUnary, Addl, Subl, Imull,
                                           Idivl, Cdq, Cmpl, Jmp, JmpCC,
                                           SetCC, AsmLabel, AllocateStack, Ret>;

export struct AsmFunction { std::string name; std::vector<AsmInstruction> instructions; };
export struct AsmProgram  { AsmFunction function; };

// ── Helpers ─────────────────────────────────────────────────────────

static Operand tacky_val_to_operand(const TackyVal& val) {
    return std::visit(Overload{
        [](const TackyConstant& c) -> Operand { return Imm{c.value}; },
        [](const TackyVar& v)      -> Operand { return Pseudo{v.name}; },
    }, val);
}

static bool is_memory(const Operand& o) {
    return std::holds_alternative<Stack>(o);
}

static void track_stack(const Operand& o, int& stack_bytes) {
    if (auto* s = std::get_if<Stack>(&o))
        stack_bytes = std::max(stack_bytes, -s->offset);
}

static void track_stack_ops(const Operand& a, const Operand& b, int& sb) {
    track_stack(a, sb);
    track_stack(b, sb);
}

// ── Pass 1: TACKY -> Assembly (with Pseudo operands) ───────────────

static CondCode convert_relational(TackyBinaryOp op) {
    switch (op) {
        case TackyBinaryOp::Equal:          return CondCode::E;
        case TackyBinaryOp::NotEqual:       return CondCode::NE;
        case TackyBinaryOp::LessThan:       return CondCode::L;
        case TackyBinaryOp::LessOrEqual:    return CondCode::LE;
        case TackyBinaryOp::GreaterThan:    return CondCode::G;
        case TackyBinaryOp::GreaterOrEqual: return CondCode::GE;
        default: return CondCode::E;
    }
}

static bool is_relational(TackyBinaryOp op) {
    return op >= TackyBinaryOp::Equal;
}

static AsmFunction tacky_to_asm(const TackyFunction& func) {
    std::vector<AsmInstruction> instructions;

    for (const auto& instr : func.instructions) {
        std::visit(Overload{
            [&](const TackyReturn& r) {
                instructions.push_back(Mov{tacky_val_to_operand(r.val), Reg{RegId::AX}});
                instructions.push_back(Ret{});
            },
            [&](const TackyUnary& u) {
                auto src = tacky_val_to_operand(u.src);
                auto dst = tacky_val_to_operand(u.dst);
                if (u.op == TackyUnaryOp::Not) {
                    instructions.push_back(Mov{src, Reg{RegId::R10}});
                    instructions.push_back(Cmpl{Imm{0}, Reg{RegId::R10}});
                    instructions.push_back(Mov{Imm{0}, Reg{RegId::R11}});
                    instructions.push_back(SetCC{CondCode::E, Reg{RegId::R11}});
                    instructions.push_back(Mov{Reg{RegId::R11}, dst});
                } else {
                    auto op = (u.op == TackyUnaryOp::Negate) ? AsmUnaryOp::Neg : AsmUnaryOp::Not;
                    instructions.push_back(Mov{src, dst});
                    instructions.push_back(AsmUnary{op, dst});
                }
            },
            [&](const TackyBinary& b) {
                auto src1 = tacky_val_to_operand(b.src1);
                auto src2 = tacky_val_to_operand(b.src2);
                auto dst  = tacky_val_to_operand(b.dst);

                if (is_relational(b.op)) {
                    instructions.push_back(Mov{src1, Reg{RegId::R10}});
                    instructions.push_back(Cmpl{src2, Reg{RegId::R10}});
                    instructions.push_back(Mov{Imm{0}, Reg{RegId::R11}});
                    instructions.push_back(SetCC{convert_relational(b.op), Reg{RegId::R11}});
                    instructions.push_back(Mov{Reg{RegId::R11}, dst});
                    return;
                }

                auto ensure_r10 = [&](const Operand& s) {
                    if (!std::holds_alternative<Reg>(s) ||
                        std::get<Reg>(s).id != RegId::R10)
                        instructions.push_back(Mov{s, Reg{RegId::R10}});
                };

                switch (b.op) {
                    case TackyBinaryOp::Add:
                    case TackyBinaryOp::Subtract:
                    case TackyBinaryOp::Multiply: {
                        instructions.push_back(Mov{src1, dst});
                        ensure_r10(src2);
                        if (b.op == TackyBinaryOp::Add)
                            instructions.push_back(Addl{Reg{RegId::R10}, dst});
                        else if (b.op == TackyBinaryOp::Subtract)
                            instructions.push_back(Subl{Reg{RegId::R10}, dst});
                        else
                            instructions.push_back(Imull{Reg{RegId::R10}, dst});
                        break;
                    }
                    case TackyBinaryOp::Divide:
                    case TackyBinaryOp::Remainder: {
                        instructions.push_back(Mov{src1, Reg{RegId::AX}});
                        instructions.push_back(Cdq{});
                        ensure_r10(src2);
                        instructions.push_back(Idivl{Reg{RegId::R10}});
                        auto result_reg = (b.op == TackyBinaryOp::Divide)
                            ? Reg{RegId::AX} : Reg{RegId::DX};
                        instructions.push_back(Mov{result_reg, dst});
                        break;
                    }
                    default: break;
                }
            },
            [&](const TackyCopy& c) {
                instructions.push_back(Mov{tacky_val_to_operand(c.src),
                                           tacky_val_to_operand(c.dst)});
            },
            [&](const TackyJump& j) {
                instructions.push_back(Jmp{j.target});
            },
            [&](const TackyJumpIfZero& j) {
                instructions.push_back(Cmpl{tacky_val_to_operand(j.condition), Imm{0}});
                instructions.push_back(JmpCC{CondCode::E, j.target});
            },
            [&](const TackyJumpIfNotZero& j) {
                instructions.push_back(Cmpl{tacky_val_to_operand(j.condition), Imm{0}});
                instructions.push_back(JmpCC{CondCode::NE, j.target});
            },
            [&](const TackyLabel& l) {
                instructions.push_back(AsmLabel{l.name});
            },
        }, instr);
    }

    return {func.name, std::move(instructions)};
}

// ── Pass 2: Replace pseudoregisters with stack locations ────────────

static Operand fix_operand(Operand o,
                           std::unordered_map<std::string, int>& offsets,
                           int& next_offset) {
    if (auto* p = std::get_if<Pseudo>(&o)) {
        if (auto it = offsets.find(p->name); it != offsets.end())
            return Stack{it->second};
        next_offset -= 4;
        offsets[p->name] = next_offset;
        return Stack{next_offset};
    }
    return o;
}

static AsmInstruction replace_pseudo(AsmInstruction instr,
                                     std::unordered_map<std::string, int>& offsets,
                                     int& next_offset) {
    return std::visit(Overload{
        [&](Mov m) -> AsmInstruction {
            return Mov{fix_operand(m.src, offsets, next_offset),
                       fix_operand(m.dst, offsets, next_offset)};
        },
        [&](AsmUnary u) -> AsmInstruction {
            return AsmUnary{u.op, fix_operand(u.operand, offsets, next_offset)};
        },
        [&](Addl a) -> AsmInstruction {
            return Addl{fix_operand(a.src, offsets, next_offset),
                        fix_operand(a.dst, offsets, next_offset)};
        },
        [&](Subl s) -> AsmInstruction {
            return Subl{fix_operand(s.src, offsets, next_offset),
                        fix_operand(s.dst, offsets, next_offset)};
        },
        [&](Imull m) -> AsmInstruction {
            return Imull{fix_operand(m.src, offsets, next_offset),
                         fix_operand(m.dst, offsets, next_offset)};
        },
        [&](Idivl d) -> AsmInstruction {
            return Idivl{fix_operand(d.operand, offsets, next_offset)};
        },
        [&](Cmpl c) -> AsmInstruction {
            return Cmpl{fix_operand(c.src, offsets, next_offset),
                        fix_operand(c.dst, offsets, next_offset)};
        },
        [&](SetCC s) -> AsmInstruction {
            return SetCC{s.cc, fix_operand(s.operand, offsets, next_offset)};
        },
        [](auto other) -> AsmInstruction { return other; },
    }, instr);
}

static AsmFunction replace_pseudos(const AsmFunction& func) {
    std::unordered_map<std::string, int> offsets;
    int next_offset = 0;

    std::vector<AsmInstruction> instructions;
    for (auto& instr : func.instructions)
        instructions.push_back(replace_pseudo(instr, offsets, next_offset));

    return {func.name, std::move(instructions)};
}

// ── Pass 3: Fix up invalid instructions ─────────────────────────────

static AsmFunction fix_up(const AsmFunction& func) {
    int stack_bytes = 0;

    std::vector<AsmInstruction> fixed;
    for (const auto& instr : func.instructions) {
        std::visit(Overload{
            [&](const Mov& m) {
                if (is_memory(m.src) && is_memory(m.dst)) {
                    fixed.push_back(Mov{m.src, Reg{RegId::R10}});
                    fixed.push_back(Mov{Reg{RegId::R10}, m.dst});
                } else {
                    fixed.push_back(m);
                }
                track_stack_ops(m.src, m.dst, stack_bytes);
            },
            [&](const AsmUnary& u) {
                track_stack(u.operand, stack_bytes);
                fixed.push_back(u);
            },
            [&](const Addl& a) {
                if (is_memory(a.src) && is_memory(a.dst)) {
                    fixed.push_back(Mov{a.src, Reg{RegId::R10}});
                    fixed.push_back(Addl{Reg{RegId::R10}, a.dst});
                } else {
                    fixed.push_back(a);
                }
                track_stack_ops(a.src, a.dst, stack_bytes);
            },
            [&](const Subl& s) {
                if (is_memory(s.src) && is_memory(s.dst)) {
                    fixed.push_back(Mov{s.src, Reg{RegId::R10}});
                    fixed.push_back(Subl{Reg{RegId::R10}, s.dst});
                } else {
                    fixed.push_back(s);
                }
                track_stack_ops(s.src, s.dst, stack_bytes);
            },
            [&](const Imull& m) {
                if (is_memory(m.dst)) {
                    fixed.push_back(Mov{m.src, Reg{RegId::AX}});
                    fixed.push_back(Mov{m.dst, Reg{RegId::R10}});
                    fixed.push_back(Imull{Reg{RegId::AX}, Reg{RegId::R10}});
                    fixed.push_back(Mov{Reg{RegId::R10}, m.dst});
                } else if (is_memory(m.src) && is_memory(m.dst)) {
                    fixed.push_back(Mov{m.src, Reg{RegId::R10}});
                    fixed.push_back(Imull{Reg{RegId::R10}, m.dst});
                } else {
                    fixed.push_back(m);
                }
                track_stack_ops(m.src, m.dst, stack_bytes);
            },
            [&](const Idivl& d) {
                track_stack(d.operand, stack_bytes);
                fixed.push_back(d);
            },
            [&](const Cmpl& c) {
                if (is_memory(c.src) && is_memory(c.dst)) {
                    fixed.push_back(Mov{c.src, Reg{RegId::R10}});
                    fixed.push_back(Cmpl{Reg{RegId::R10}, c.dst});
                } else if (std::holds_alternative<Imm>(c.dst)) {
                    fixed.push_back(Mov{c.dst, Reg{RegId::R11}});
                    fixed.push_back(Cmpl{c.src, Reg{RegId::R11}});
                } else {
                    fixed.push_back(c);
                }
                track_stack_ops(c.src, c.dst, stack_bytes);
            },
            [&](const SetCC& s) {
                track_stack(s.operand, stack_bytes);
                fixed.push_back(s);
            },
            [&](const auto& other) { fixed.push_back(other); },
        }, instr);
    }

    std::vector<AsmInstruction> result;
    if (stack_bytes > 0)
        result.push_back(AllocateStack{stack_bytes});
    std::move(fixed.begin(), fixed.end(), std::back_inserter(result));

    return {func.name, std::move(result)};
}

// ── Full pipeline ───────────────────────────────────────────────────

export AsmProgram codegen(const Program& program) {
    auto tacky = emit_tacky(program);
    auto asm_func = tacky_to_asm(tacky.function);
    asm_func = replace_pseudos(asm_func);
    asm_func = fix_up(asm_func);
    return {std::move(asm_func)};
}

// ── Assembly -> Text ────────────────────────────────────────────────

static std::string platform_prefix() {
#if defined(__APPLE__)
    return "_";
#else
    return "";
#endif
}

static std::string operand_str(const Operand& o) {
    return std::visit(Overload{
        [](const Imm& i) -> std::string { return "$" + std::to_string(i.value); },
        [](const Reg& r) -> std::string {
            switch (r.id) {
                case RegId::AX:  return "%eax";
                case RegId::DX:  return "%edx";
                case RegId::R10: return "%r10d";
                case RegId::R11: return "%r11d";
            }
            return "%eax";
        },
        [](const Stack& s) -> std::string {
            return std::to_string(s.offset) + "(%rbp)";
        },
        [](const Pseudo&) -> std::string { return "<pseudo>"; },
    }, o);
}

static std::string operand_byte_str(const Operand& o) {
    return std::visit(Overload{
        [](const Imm& i) -> std::string { return "$" + std::to_string(i.value); },
        [](const Reg& r) -> std::string {
            switch (r.id) {
                case RegId::AX:  return "%al";
                case RegId::DX:  return "%dl";
                case RegId::R10: return "%r10b";
                case RegId::R11: return "%r11b";
            }
            return "%al";
        },
        [](const Stack& s) -> std::string {
            return std::to_string(s.offset) + "(%rbp)";
        },
        [](const Pseudo&) -> std::string { return "<pseudo>"; },
    }, o);
}

static std::string cond_code_str(CondCode cc) {
    switch (cc) {
        case CondCode::E:  return "e";
        case CondCode::NE: return "ne";
        case CondCode::G:  return "g";
        case CondCode::GE: return "ge";
        case CondCode::L:  return "l";
        case CondCode::LE: return "le";
    }
    return "e";
}

export void emit_asm(const AsmProgram& program, std::string& output) {
    auto prefix = platform_prefix();
    auto& name = program.function.name;

    output += "    .text\n";
    output += "    .globl " + prefix + name + "\n";
    output += prefix + name + ":\n";
    output += "    pushq %rbp\n";
    output += "    movq %rsp, %rbp\n";

    for (const auto& instr : program.function.instructions) {
        std::visit(Overload{
            [&output](const Mov& m) {
                output += "    movl " + operand_str(m.src) + ", " + operand_str(m.dst) + "\n";
            },
            [&output](const AsmUnary& u) {
                std::string op_name = (u.op == AsmUnaryOp::Neg) ? "negl" : "notl";
                output += "    " + op_name + " " + operand_str(u.operand) + "\n";
            },
            [&output](const Addl& a) {
                output += "    addl " + operand_str(a.src) + ", " + operand_str(a.dst) + "\n";
            },
            [&output](const Subl& s) {
                output += "    subl " + operand_str(s.src) + ", " + operand_str(s.dst) + "\n";
            },
            [&output](const Imull& m) {
                output += "    imull " + operand_str(m.src) + ", " + operand_str(m.dst) + "\n";
            },
            [&output](const Idivl& d) {
                output += "    idivl " + operand_str(d.operand) + "\n";
            },
            [&output](const Cdq&) {
                output += "    cdq\n";
            },
            [&output](const Cmpl& c) {
                output += "    cmpl " + operand_str(c.src) + ", " + operand_str(c.dst) + "\n";
            },
            [&output](const Jmp& j) {
                output += "    jmp " + j.target + "\n";
            },
            [&output](const JmpCC& j) {
                output += "    j" + cond_code_str(j.cc) + " " + j.target + "\n";
            },
            [&output](const SetCC& s) {
                output += "    set" + cond_code_str(s.cc) + " " + operand_byte_str(s.operand) + "\n";
            },
            [&output](const AsmLabel& l) {
                output += l.name + ":\n";
            },
            [&output](const AllocateStack& a) {
                output += "    subq $" + std::to_string(a.bytes) + ", %rsp\n";
            },
            [&output](const Ret&) {
                output += "    movq %rbp, %rsp\n";
                output += "    popq %rbp\n";
                output += "    ret\n";
            },
        }, instr);
    }

#if defined(__linux__)
    output += "\n    .section .note.GNU-stack,\"\",@progbits\n";
#endif
}
