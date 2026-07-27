module;

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

export module codegen;

import ast;
import tacky;


export enum class RegId : uint8_t { AX, CX, DX, R10, R11 };

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
export struct Andl     { Operand src; Operand dst; };
export struct Orl      { Operand src; Operand dst; };
export struct Xorl     { Operand src; Operand dst; };
export struct Shll     { Operand operand; };
export struct Sarl     { Operand operand; };

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
                                           Idivl, Cdq, Cmpl, Andl, Orl, Xorl,
                                           Shll, Sarl, Jmp, JmpCC,
                                           SetCC, AsmLabel, AllocateStack, Ret>;

export struct AsmFunction { std::string name; std::vector<AsmInstruction> instructions; };
export struct AsmProgram  { AsmFunction function; };


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
    return op == TackyBinaryOp::Equal || op == TackyBinaryOp::NotEqual ||
           op == TackyBinaryOp::LessThan || op == TackyBinaryOp::LessOrEqual ||
           op == TackyBinaryOp::GreaterThan || op == TackyBinaryOp::GreaterOrEqual;
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
                    case TackyBinaryOp::BitwiseAnd:
                    case TackyBinaryOp::BitwiseOr:
                    case TackyBinaryOp::BitwiseXor: {
                        instructions.push_back(Mov{src1, dst});
                        ensure_r10(src2);
                        if (b.op == TackyBinaryOp::Add)
                            instructions.push_back(Addl{Reg{RegId::R10}, dst});
                        else if (b.op == TackyBinaryOp::BitwiseAnd)
                            instructions.push_back(Andl{Reg{RegId::R10}, dst});
                        else if (b.op == TackyBinaryOp::BitwiseOr)
                            instructions.push_back(Orl{Reg{RegId::R10}, dst});
                        else
                            instructions.push_back(Xorl{Reg{RegId::R10}, dst});
                        break;
                    }
                    case TackyBinaryOp::Subtract:
                    case TackyBinaryOp::Multiply: {
                        instructions.push_back(Mov{src1, dst});
                        ensure_r10(src2);
                        if (b.op == TackyBinaryOp::Subtract)
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
                    case TackyBinaryOp::ShiftLeft:
                    case TackyBinaryOp::ShiftRight: {
                        instructions.push_back(Mov{src2, Reg{RegId::CX}});
                        instructions.push_back(Mov{src1, dst});
                        if (b.op == TackyBinaryOp::ShiftLeft)
                            instructions.push_back(Shll{dst});
                        else
                            instructions.push_back(Sarl{dst});
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
        [&](Andl a) -> AsmInstruction {
            return Andl{fix_operand(a.src, offsets, next_offset),
                        fix_operand(a.dst, offsets, next_offset)};
        },
        [&](Orl o) -> AsmInstruction {
            return Orl{fix_operand(o.src, offsets, next_offset),
                       fix_operand(o.dst, offsets, next_offset)};
        },
        [&](Xorl x) -> AsmInstruction {
            return Xorl{fix_operand(x.src, offsets, next_offset),
                        fix_operand(x.dst, offsets, next_offset)};
        },
        [&](Shll s) -> AsmInstruction {
            return Shll{fix_operand(s.operand, offsets, next_offset)};
        },
        [&](Sarl s) -> AsmInstruction {
            return Sarl{fix_operand(s.operand, offsets, next_offset)};
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
            [&](const Andl& a) {
                if (is_memory(a.src) && is_memory(a.dst)) {
                    fixed.push_back(Mov{a.src, Reg{RegId::R10}});
                    fixed.push_back(Andl{Reg{RegId::R10}, a.dst});
                } else {
                    fixed.push_back(a);
                }
                track_stack_ops(a.src, a.dst, stack_bytes);
            },
            [&](const Orl& o) {
                if (is_memory(o.src) && is_memory(o.dst)) {
                    fixed.push_back(Mov{o.src, Reg{RegId::R10}});
                    fixed.push_back(Orl{Reg{RegId::R10}, o.dst});
                } else {
                    fixed.push_back(o);
                }
                track_stack_ops(o.src, o.dst, stack_bytes);
            },
            [&](const Xorl& x) {
                if (is_memory(x.src) && is_memory(x.dst)) {
                    fixed.push_back(Mov{x.src, Reg{RegId::R10}});
                    fixed.push_back(Xorl{Reg{RegId::R10}, x.dst});
                } else {
                    fixed.push_back(x);
                }
                track_stack_ops(x.src, x.dst, stack_bytes);
            },
            [&](const Shll& s) {
                track_stack(s.operand, stack_bytes);
                fixed.push_back(s);
            },
            [&](const Sarl& s) {
                track_stack(s.operand, stack_bytes);
                fixed.push_back(s);
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


export AsmProgram codegen(const Program& program) {
    auto tacky = emit_tacky(program);
    auto asm_func = tacky_to_asm(tacky.function);
    asm_func = replace_pseudos(asm_func);
    asm_func = fix_up(asm_func);
    return {std::move(asm_func)};
}


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
                case RegId::CX:  return "%ecx";
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
                case RegId::CX:  return "%cl";
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
            [&output](const Andl& a) {
                output += "    andl " + operand_str(a.src) + ", " + operand_str(a.dst) + "\n";
            },
            [&output](const Orl& o) {
                output += "    orl " + operand_str(o.src) + ", " + operand_str(o.dst) + "\n";
            },
            [&output](const Xorl& x) {
                output += "    xorl " + operand_str(x.src) + ", " + operand_str(x.dst) + "\n";
            },
            [&output](const Shll& s) {
                output += "    shll %cl, " + operand_str(s.operand) + "\n";
            },
            [&output](const Sarl& s) {
                output += "    sarl %cl, " + operand_str(s.operand) + "\n";
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
