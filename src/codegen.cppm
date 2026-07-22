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

export struct TackyConstant {
    int32_t value;
};

export struct TackyVar {
    std::string name;
};

export using TackyVal = std::variant<TackyConstant, TackyVar>;

export struct TackyComplement {};
export struct TackyNegate {};

export using TackyUnaryOp = std::variant<TackyComplement, TackyNegate>;

export struct TackyAdd {};
export struct TackySubtract {};
export struct TackyMultiply {};
export struct TackyDivide {};
export struct TackyRemainder {};

export using TackyBinaryOp = std::variant<TackyAdd, TackySubtract, TackyMultiply,
                                           TackyDivide, TackyRemainder>;

export struct TackyReturn {
    TackyVal val;
};

export struct TackyUnary {
    TackyUnaryOp op;
    TackyVal src;
    TackyVal dst;
};

export struct TackyBinary {
    TackyBinaryOp op;
    TackyVal src1;
    TackyVal src2;
    TackyVal dst;
};

export using TackyInstruction = std::variant<TackyReturn, TackyUnary, TackyBinary>;

export struct TackyFunction {
    std::string name;
    std::vector<TackyInstruction> instructions;
};

export struct TackyProgram {
    TackyFunction function;
};

// ── TACKY generation (AST → TACKY) ─────────────────────────────────

static int temp_counter = 0;

static std::string make_temporary() {
    return "tmp." + std::to_string(temp_counter++);
}

static TackyVal emit_tacky_val(const Exp& exp,
                                std::vector<TackyInstruction>& instructions) {
    return std::visit(Overload{
        [](const Constant& c) -> TackyVal {
            return TackyConstant{c.value};
        },
        [&](const Unary& u) -> TackyVal {
            auto src = emit_tacky_val(*u.exp, instructions);
            auto dst_name = make_temporary();
            TackyVal dst = TackyVar{std::move(dst_name)};
            auto tacky_op = std::visit(Overload{
                [](const Complement&) -> TackyUnaryOp { return TackyComplement{}; },
                [](const Negate&) -> TackyUnaryOp { return TackyNegate{}; },
            }, u.op);
            instructions.push_back(TackyUnary{tacky_op, src, dst});
            return dst;
        },
        [&](const Binary& b) -> TackyVal {
            auto left = emit_tacky_val(*b.left, instructions);
            auto right = emit_tacky_val(*b.right, instructions);
            auto dst_name = make_temporary();
            TackyVal dst = TackyVar{std::move(dst_name)};
            auto tacky_op = std::visit(Overload{
                [](const Add&) -> TackyBinaryOp { return TackyAdd{}; },
                [](const Subtract&) -> TackyBinaryOp { return TackySubtract{}; },
                [](const Multiply&) -> TackyBinaryOp { return TackyMultiply{}; },
                [](const Divide&) -> TackyBinaryOp { return TackyDivide{}; },
                [](const Remainder&) -> TackyBinaryOp { return TackyRemainder{}; },
            }, b.op);
            instructions.push_back(TackyBinary{tacky_op, left, right, dst});
            return dst;
        },
    }, exp.value);
}

static TackyFunction emit_tacky_function(const Function& func) {
    std::vector<TackyInstruction> instructions;
    std::visit(Overload{
        [&](const Return& r) {
            auto val = emit_tacky_val(r.value, instructions);
            instructions.push_back(TackyReturn{val});
        },
    }, func.body);
    return {std::string(func.name), std::move(instructions)};
}

export TackyProgram emit_tacky(const Program& program) {
    temp_counter = 0;
    return {emit_tacky_function(program.function)};
}

// ── Assembly AST ────────────────────────────────────────────────────

export enum class RegId : uint8_t { AX, DX, R10 };

export struct Reg {
    RegId id;
};

export struct Imm {
    int32_t value;
};

export struct Pseudo {
    std::string name;
};

export struct Stack {
    int offset;
};

export using Operand = std::variant<Imm, Reg, Pseudo, Stack>;

export struct Mov {
    Operand src;
    Operand dst;
};

export struct AsmNeg {};
export struct AsmNot {};

export using AsmUnaryOp = std::variant<AsmNeg, AsmNot>;

export struct AsmUnary {
    AsmUnaryOp op;
    Operand operand;
};

export struct Addl {
    Operand src;
    Operand dst;
};

export struct Subl {
    Operand src;
    Operand dst;
};

export struct Imull {
    Operand src;
    Operand dst;
};

export struct Idivl {
    Operand operand;
};

export struct Cdq {};

export struct AllocateStack {
    int bytes;
};

export struct Ret {};

export using AsmInstruction = std::variant<Mov, AsmUnary, Addl, Subl, Imull,
                                           Idivl, Cdq, AllocateStack, Ret>;

export struct AsmFunction {
    std::string name;
    std::vector<AsmInstruction> instructions;
};

export struct AsmProgram {
    AsmFunction function;
};

// ── Pass 1: TACKY → Assembly (with Pseudo operands) ────────────────

static Operand tacky_val_to_operand(const TackyVal& val) {
    return std::visit(Overload{
        [](const TackyConstant& c) -> Operand { return Imm{c.value}; },
        [](const TackyVar& v) -> Operand { return Pseudo{v.name}; },
    }, val);
}

static AsmUnaryOp convert_unop(const TackyUnaryOp& op) {
    return std::visit(Overload{
        [](const TackyComplement&) -> AsmUnaryOp { return AsmNot{}; },
        [](const TackyNegate&) -> AsmUnaryOp { return AsmNeg{}; },
    }, op);
}

static AsmFunction tacky_to_asm(const TackyFunction& func) {
    std::vector<AsmInstruction> instructions;

    for (const auto& instr : func.instructions) {
        std::visit(Overload{
            [&](const TackyReturn& r) {
                instructions.push_back(Mov{tacky_val_to_operand(r.val),
                                           Reg{RegId::AX}});
                instructions.push_back(Ret{});
            },
            [&](const TackyUnary& u) {
                auto src = tacky_val_to_operand(u.src);
                auto dst = tacky_val_to_operand(u.dst);
                instructions.push_back(Mov{src, dst});
                instructions.push_back(AsmUnary{convert_unop(u.op), dst});
            },
            [&](const TackyBinary& b) {
                auto src1 = tacky_val_to_operand(b.src1);
                auto src2 = tacky_val_to_operand(b.src2);
                auto dst = tacky_val_to_operand(b.dst);

                std::visit(Overload{
                    [&](const TackyAdd&) {
                        instructions.push_back(Mov{src1, dst});
                        instructions.push_back(Mov{src2, Reg{RegId::R10}});
                        instructions.push_back(Addl{Reg{RegId::R10}, dst});
                    },
                    [&](const TackySubtract&) {
                        instructions.push_back(Mov{src1, dst});
                        instructions.push_back(Mov{src2, Reg{RegId::R10}});
                        instructions.push_back(Subl{Reg{RegId::R10}, dst});
                    },
                    [&](const TackyMultiply&) {
                        instructions.push_back(Mov{src1, dst});
                        instructions.push_back(Mov{src2, Reg{RegId::R10}});
                        instructions.push_back(Imull{Reg{RegId::R10}, dst});
                    },
                    [&](const TackyDivide&) {
                        instructions.push_back(Mov{src1, Reg{RegId::AX}});
                        instructions.push_back(Cdq{});
                        if (!std::holds_alternative<Reg>(src2) ||
                            std::get<Reg>(src2).id != RegId::R10) {
                            instructions.push_back(Mov{src2, Reg{RegId::R10}});
                        }
                        instructions.push_back(Idivl{Reg{RegId::R10}});
                        instructions.push_back(Mov{Reg{RegId::AX}, dst});
                    },
                    [&](const TackyRemainder&) {
                        instructions.push_back(Mov{src1, Reg{RegId::AX}});
                        instructions.push_back(Cdq{});
                        if (!std::holds_alternative<Reg>(src2) ||
                            std::get<Reg>(src2).id != RegId::R10) {
                            instructions.push_back(Mov{src2, Reg{RegId::R10}});
                        }
                        instructions.push_back(Idivl{Reg{RegId::R10}});
                        instructions.push_back(Mov{Reg{RegId::DX}, dst});
                    },
                }, b.op);
            },
        }, instr);
    }

    return {func.name, std::move(instructions)};
}

// ── Pass 2: Replace pseudoregisters with stack locations ────────────

static Operand fix_pseudo(Operand o,
                           std::unordered_map<std::string, int>& offsets,
                           int& next_offset) {
    return std::visit(Overload{
        [&](Pseudo& p) -> Operand {
            auto it = offsets.find(p.name);
            if (it == offsets.end()) {
                next_offset -= 4;
                offsets[p.name] = next_offset;
                return Stack{next_offset};
            }
            return Stack{it->second};
        },
        [&](auto& other) -> Operand { return other; },
    }, o);
}

static AsmFunction replace_pseudos(const AsmFunction& func) {
    std::unordered_map<std::string, int> offsets;
    int next_offset = 0;

    std::vector<AsmInstruction> instructions;
    for (auto& instr : func.instructions) {
        instructions.push_back(std::visit(Overload{
            [&](Mov m) -> AsmInstruction {
                return Mov{fix_pseudo(m.src, offsets, next_offset),
                           fix_pseudo(m.dst, offsets, next_offset)};
            },
            [&](AsmUnary u) -> AsmInstruction {
                return AsmUnary{u.op, fix_pseudo(u.operand, offsets, next_offset)};
            },
            [&](Addl a) -> AsmInstruction {
                return Addl{fix_pseudo(a.src, offsets, next_offset),
                            fix_pseudo(a.dst, offsets, next_offset)};
            },
            [&](Subl s) -> AsmInstruction {
                return Subl{fix_pseudo(s.src, offsets, next_offset),
                            fix_pseudo(s.dst, offsets, next_offset)};
            },
            [&](Imull m) -> AsmInstruction {
                return Imull{fix_pseudo(m.src, offsets, next_offset),
                             fix_pseudo(m.dst, offsets, next_offset)};
            },
            [&](Idivl d) -> AsmInstruction {
                return Idivl{fix_pseudo(d.operand, offsets, next_offset)};
            },
            [](auto other) -> AsmInstruction { return other; },
        }, instr));
    }

    return {func.name, std::move(instructions)};
}

// ── Pass 3: Fix up invalid instructions ─────────────────────────────

static bool is_memory_operand(const Operand& o) {
    return std::holds_alternative<Stack>(o);
}

static AsmFunction fix_up(const AsmFunction& func) {
    int stack_bytes = 0;

    std::vector<AsmInstruction> fixed;
    for (const auto& instr : func.instructions) {
        std::visit(Overload{
            [&](const Mov& m) {
                if (is_memory_operand(m.src) && is_memory_operand(m.dst)) {
                    fixed.push_back(Mov{m.src, Reg{RegId::R10}});
                    fixed.push_back(Mov{Reg{RegId::R10}, m.dst});
                } else {
                    fixed.push_back(m);
                }
                auto update = [&](const Operand& o) {
                    if (auto* s = std::get_if<Stack>(&o))
                        stack_bytes = std::max(stack_bytes, -s->offset);
                };
                update(m.src);
                update(m.dst);
            },
            [&](const AsmUnary& u) {
                if (auto* s = std::get_if<Stack>(&u.operand))
                    stack_bytes = std::max(stack_bytes, -s->offset);
                fixed.push_back(u);
            },
            [&](const Addl& a) {
                if (is_memory_operand(a.src) && is_memory_operand(a.dst)) {
                    fixed.push_back(Mov{a.src, Reg{RegId::R10}});
                    fixed.push_back(Addl{Reg{RegId::R10}, a.dst});
                } else {
                    fixed.push_back(a);
                }
                auto update = [&](const Operand& o) {
                    if (auto* s = std::get_if<Stack>(&o))
                        stack_bytes = std::max(stack_bytes, -s->offset);
                };
                update(a.src);
                update(a.dst);
            },
            [&](const Subl& s) {
                if (is_memory_operand(s.src) && is_memory_operand(s.dst)) {
                    fixed.push_back(Mov{s.src, Reg{RegId::R10}});
                    fixed.push_back(Subl{Reg{RegId::R10}, s.dst});
                } else {
                    fixed.push_back(s);
                }
                auto update = [&](const Operand& o) {
                    if (auto* s = std::get_if<Stack>(&o))
                        stack_bytes = std::max(stack_bytes, -s->offset);
                };
                update(s.src);
                update(s.dst);
            },
            [&](const Imull& m) {
                if (is_memory_operand(m.dst)) {
                    fixed.push_back(Mov{m.src, Reg{RegId::AX}});
                    fixed.push_back(Mov{m.dst, Reg{RegId::R10}});
                    fixed.push_back(Imull{Reg{RegId::AX}, Reg{RegId::R10}});
                    fixed.push_back(Mov{Reg{RegId::R10}, m.dst});
                } else if (is_memory_operand(m.src) && is_memory_operand(m.dst)) {
                    fixed.push_back(Mov{m.src, Reg{RegId::R10}});
                    fixed.push_back(Imull{Reg{RegId::R10}, m.dst});
                } else {
                    fixed.push_back(m);
                }
                auto update = [&](const Operand& o) {
                    if (auto* s = std::get_if<Stack>(&o))
                        stack_bytes = std::max(stack_bytes, -s->offset);
                };
                update(m.src);
                update(m.dst);
            },
            [&](const Idivl& d) {
                if (auto* s = std::get_if<Stack>(&d.operand))
                    stack_bytes = std::max(stack_bytes, -s->offset);
                fixed.push_back(d);
            },
            [&](const auto& other) { fixed.push_back(other); },
        }, instr);
    }

    std::vector<AsmInstruction> result;
    if (stack_bytes > 0) {
        result.push_back(AllocateStack{stack_bytes});
    }
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

// ── Assembly → Text ─────────────────────────────────────────────────

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
            }
            return "%eax";
        },
        [](const Stack& s) -> std::string {
            return std::to_string(s.offset) + "(%rbp)";
        },
        [](const Pseudo&) -> std::string { return "<pseudo>"; },
    }, o);
}

export void emit_asm(const AsmProgram& program, std::string& output) {
    auto prefix = platform_prefix();
    auto& name = program.function.name;

    output += "    .text\n";
    output += "    .globl " + prefix + name + "\n";
    output += prefix + name + ":\n";

    // Prologue
    output += "    pushq %rbp\n";
    output += "    movq %rsp, %rbp\n";

    for (const auto& instr : program.function.instructions) {
        std::visit(Overload{
            [&output](const Mov& m) {
                output += "    movl " + operand_str(m.src) + ", " + operand_str(m.dst) + "\n";
            },
            [&output](const AsmUnary& u) {
                auto op_name = std::visit(Overload{
                    [](const AsmNeg&) -> std::string { return "negl"; },
                    [](const AsmNot&) -> std::string { return "notl"; },
                }, u.op);
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
