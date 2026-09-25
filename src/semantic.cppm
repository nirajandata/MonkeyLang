module;

#include <cstdint>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

export module semantic;

import ast;

namespace {
std::uint64_t name_counter = 0;
}

export std::uint64_t next_name_id();

namespace {
class VariableResolver {
  std::unordered_map<std::string, std::string> variables_;
  bool had_error_ = false;

  std::string make_unique_name(std::string_view name) {
    return std::string(name) + "." + std::to_string(next_name_id());
  }

  void resolve_exp(Exp &exp) {
    std::visit(
        Overload{
            [](Constant &) {},
            [&](Var &v) {
              auto it = variables_.find(v.name);
              if (it == variables_.end()) {
                std::println("error:{}: Undeclared variable '{}'", v.line,
                             v.name);
                had_error_ = true;
                return;
              }
              v.name = it->second;
            },
            [&](Unary &u) { resolve_exp(*u.exp); },
            [&](Binary &b) {
              resolve_exp(*b.left);
              resolve_exp(*b.right);
            },
            [&](Assignment &a) {
              if (!std::holds_alternative<Var>(a.left->value)) {
                std::println(
                    "error:{}: Expression is not a valid lvalue", a.line);
                had_error_ = true;
              }
              resolve_exp(*a.left);
              resolve_exp(*a.right);
            },
        },
        exp.value);
  }

  void resolve_statement(Statement &stmt) {
    std::visit(
        Overload{
            [&](Return &r) { resolve_exp(r.value); },
            [&](Expression &e) { resolve_exp(e.value); },
            [&](Null &) {},
        },
        stmt);
  }

  void resolve_block_item(BlockItem &item) {
    std::visit(
        Overload{
            [&](Statement &stmt) { resolve_statement(stmt); },
            [&](Declaration &d) {
              if (variables_.contains(d.name)) {
                std::println("error:{}: Duplicate variable '{}'", d.line,
                             d.name);
                had_error_ = true;
              } else {
                auto unique_name = make_unique_name(d.name);
                variables_.emplace(d.name, unique_name);
                d.name = std::move(unique_name);
              }
              if (d.init) resolve_exp(*d.init);
            },
        },
        item);
  }

public:
  bool resolve(Program &program) {
    for (auto &item : program.function.body) resolve_block_item(item);
    return !had_error_;
  }
};
}

export std::uint64_t next_name_id() {
  return name_counter++;
}

export bool resolve_variables(Program &program) {
  VariableResolver resolver;
  return resolver.resolve(program);
}
