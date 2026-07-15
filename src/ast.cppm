module;

#include <cstdint>
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

export using Exp = std::variant<Constant>;

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
