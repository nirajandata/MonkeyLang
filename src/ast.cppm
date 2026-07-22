module;

#include <cstdint>
#include <memory>
#include <string_view>
#include <variant>
#include <utility>

export module ast;

export template <typename... Ts>
struct Overload : Ts... {
    using Ts::operator()...;
};

export struct Constant {
    int32_t value;
    uint32_t line;
};

export struct Complement {};
export struct Negate {};

export using UnaryOp = std::variant<Complement, Negate>;

export struct Add {};
export struct Subtract {};
export struct Multiply {};
export struct Divide {};
export struct Remainder {};

export using BinaryOp = std::variant<Add, Subtract, Multiply, Divide, Remainder>;

export struct Exp;

export struct Unary {
    UnaryOp op;
    std::unique_ptr<Exp> exp;
    uint32_t line;
};

export struct Binary {
    BinaryOp op;
    std::unique_ptr<Exp> left;
    std::unique_ptr<Exp> right;
    uint32_t line;
};

export struct Exp {
    std::variant<Constant, Unary, Binary> value;
};

export struct Return {
    Exp value;
    uint32_t line;
};

export using Statement = std::variant<Return>;

export struct Function {
    std::string_view name;
    Statement body;
    uint32_t line;
};

export struct Program {
    Function function;
};
