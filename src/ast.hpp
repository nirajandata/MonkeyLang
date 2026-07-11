#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

struct Constant {
    int32_t value;
    uint32_t line;
};

using Exp = std::variant<Constant>;

struct Return {
    Exp value;
    uint32_t line;
};

using Statement = std::variant<Return>;

struct Function {
    std::string_view name;
    Statement body;
    uint32_t line;
};

struct Program {
    Function function;
};
