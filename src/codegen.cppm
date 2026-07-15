module;

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module codegen;

import ast;

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

export using Instruction = std::variant<Mov, Ret>;

export struct AsmFunction {
    std::string name;
    std::vector<Instruction> instructions;
};

export struct AsmProgram {
    AsmFunction function;
};

export Operand gen_exp(const Exp& exp) {
    return std::visit(Overload{
        [](const Constant& c) -> Operand { return Imm{c.value}; },
    }, exp);
}

export std::vector<Instruction> gen_statement(const Statement& stmt) {
    return std::visit(Overload{
        [](const Return& r) -> std::vector<Instruction> {
            auto operand = gen_exp(r.value);
            return {Mov{operand, Register{}}, Ret{}};
        },
    }, stmt);
}

export AsmFunction gen_function(const Function& func) {
    std::vector<Instruction> instructions = gen_statement(func.body);
    return {std::string(func.name), std::move(instructions)};
}

export AsmProgram codegen(const Program& program) {
    return {gen_function(program.function)};
}

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
        }, instr);
    }

#if defined(__linux__)
    output += "\n    .section .note.GNU-stack,\"\",@progbits\n";
#endif
}
