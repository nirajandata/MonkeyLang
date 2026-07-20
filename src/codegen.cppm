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

export struct TackyReturn {
    TackyVal val;
};

export struct TackyUnary {
    TackyUnaryOp op;
    TackyVal src;
    TackyVal dst;
};

export using TackyInstruction = std::variant<TackyReturn, TackyUnary>;

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

export enum class RegId : uint8_t { AX, R10 };

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

export struct AllocateStack {
    int bytes;
};

export struct Ret {};

export using AsmInstruction = std::variant<Mov, AsmUnary, AllocateStack, Ret>;

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
        }, instr);
    }

    return {func.name, std::move(instructions)};
}

// ── Pass 2: Replace pseudoregisters with stack locations ────────────

static AsmInstruction replace_pseudo(AsmInstruction instr,
                                      std::unordered_map<std::string, int>& offsets,
                                      int& next_offset) {
    return std::visit(Overload{
        [&](Mov m) -> AsmInstruction {
            auto fix = [&](Operand o) -> Operand {
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
            };
            return Mov{fix(m.src), fix(m.dst)};
        },
        [&](AsmUnary u) -> AsmInstruction {
            auto fix = [&](Operand o) -> Operand {
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
            };
            return AsmUnary{u.op, fix(u.operand)};
        },
        [](auto other) -> AsmInstruction { return other; },
    }, instr);
}

static AsmFunction replace_pseudos(const AsmFunction& func) {
    std::unordered_map<std::string, int> offsets;
    int next_offset = 0;

    std::vector<AsmInstruction> instructions;
    for (auto& instr : func.instructions) {
        instructions.push_back(replace_pseudo(instr, offsets, next_offset));
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
                    if (auto* s = std::get_if<Stack>(&o)) {
                        stack_bytes = std::max(stack_bytes, -s->offset);
                    }
                };
                update(m.src);
                update(m.dst);
            },
            [&](const AsmUnary& u) {
                if (auto* s = std::get_if<Stack>(&u.operand)) {
                    stack_bytes = std::max(stack_bytes, -s->offset);
                }
                fixed.push_back(u);
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
            [&output](const AllocateStack& a) {
                output += "    subq $" + std::to_string(a.bytes) + ", %rsp\n";
            },
            [&output](const Ret&) {
                // Epilogue
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
