module;

#include <cstdint>
#include <string>
#include <string_view>
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

export struct Register {};

export struct Imm {
    int32_t value;
};

export using Operand = std::variant<Imm, Register>;

export struct Mov {
    Operand src;
    Operand dst;
};

export struct Ret {};

export struct Notl {};

export struct Negl {};

export using Instruction = std::variant<Mov, Ret, Notl, Negl>;

export struct AsmFunction {
    std::string name;
    std::vector<Instruction> instructions;
};

export struct AsmProgram {
    AsmFunction function;
};

// ── TACKY → Assembly ───────────────────────────────────────────────

static Operand tacky_val_to_operand(const TackyVal& val) {
    return std::visit(Overload{
        [](const TackyConstant& c) -> Operand { return Imm{c.value}; },
        [](const TackyVar&) -> Operand { return Register{}; },
    }, val);
}

static AsmFunction tacky_to_asm(const TackyFunction& func) {
    std::vector<Instruction> instructions;

    for (const auto& instr : func.instructions) {
        std::visit(Overload{
            [&](const TackyReturn& r) {
                if (std::holds_alternative<TackyConstant>(r.val)) {
                    instructions.push_back(Mov{tacky_val_to_operand(r.val), Register{}});
                }
                instructions.push_back(Ret{});
            },
            [&](const TackyUnary& u) {
                if (std::holds_alternative<TackyConstant>(u.src)) {
                    instructions.push_back(Mov{tacky_val_to_operand(u.src), Register{}});
                }
                std::visit(Overload{
                    [&](const TackyComplement&) { instructions.push_back(Notl{}); },
                    [&](const TackyNegate&) { instructions.push_back(Negl{}); },
                }, u.op);
            },
        }, instr);
    }

    return {func.name, std::move(instructions)};
}

export AsmProgram tacky_to_asm(const TackyProgram& program) {
    return {tacky_to_asm(program.function)};
}

// ── Full pipeline: AST → TACKY → ASM ───────────────────────────────

export AsmProgram codegen(const Program& program) {
    auto tacky = emit_tacky(program);
    return tacky_to_asm(tacky);
}

// ── Assembly → Text ─────────────────────────────────────────────────

static std::string platform_prefix() {
#if defined(__APPLE__)
    return "_";
#else
    return "";
#endif
}

export void emit_asm(const AsmProgram& program, std::string& output) {
    auto prefix = platform_prefix();
    auto& name = program.function.name;

    output += "    .text\n";
    output += "    .globl " + prefix + name + "\n";
    output += prefix + name + ":\n";

    for (const auto& instr : program.function.instructions) {
        std::visit(Overload{
            [&output](const Mov& m) {
                auto operand_str = [](const Operand& o) -> std::string {
                    return std::visit(Overload{
                        [](const Imm& i) -> std::string { return "$" + std::to_string(i.value); },
                        [](const Register&) -> std::string { return "%eax"; },
                    }, o);
                };
                output += "    movl " + operand_str(m.src) + ", " + operand_str(m.dst) + "\n";
            },
            [&output](const Ret&) { output += "    ret\n"; },
            [&output](const Notl&) { output += "    notl %eax\n"; },
            [&output](const Negl&) { output += "    negl %eax\n"; },
        }, instr);
    }

#if defined(__linux__)
    output += "\n    .section .note.GNU-stack,\"\",@progbits\n";
#endif
}
