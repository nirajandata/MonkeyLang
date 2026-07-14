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
    return std::visit([](const auto& e) -> Operand {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, Constant>) {
            return Imm{e.value};
        }
    }, exp);
}

export std::vector<Instruction> gen_statement(const Statement& stmt) {
    return std::visit([](const auto& s) -> std::vector<Instruction> {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, Return>) {
            auto operand = gen_exp(s.value);
            return {Mov{operand, Register{}}, Ret{}};
        }
    }, stmt);
}

export AsmFunction gen_function(const Function& func) {
    std::vector<Instruction> instructions = gen_statement(func.body);
    return {std::string(func.name), std::move(instructions)};
}

export AsmProgram codegen(const Program& program) {
    return {gen_function(program.function)};
}

export void emit_asm(const AsmProgram& program, std::string& output) {
    output += "    .text\n";
    output += "    .globl " + program.function.name + "\n";
    output += program.function.name + ":\n";

    for (const auto& instr : program.function.instructions) {
        std::visit([&output](const auto& i) {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, Mov>) {
                std::string src = std::visit([](const auto& o) -> std::string {
                    using O = std::decay_t<decltype(o)>;
                    if constexpr (std::is_same_v<O, Imm>) {
                        return "$" + std::to_string(o.value);
                    } else if constexpr (std::is_same_v<O, Register>) {
                        return "%eax";
                    }
                }, i.src);

                std::string dst = std::visit([](const auto& o) -> std::string {
                    using O = std::decay_t<decltype(o)>;
                    if constexpr (std::is_same_v<O, Imm>) {
                        return "$" + std::to_string(o.value);
                    } else if constexpr (std::is_same_v<O, Register>) {
                        return "%eax";
                    }
                }, i.dst);

                output += "    movl " + src + ", " + dst + "\n";
            } else if constexpr (std::is_same_v<T, Ret>) {
                output += "    ret\n";
            }
        }, instr);
    }
}
