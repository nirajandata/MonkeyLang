module;

#include <cstdint>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

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

  static std::string make_unique_name(const std::string_view name) {
    return std::string(name) + "." + std::to_string(next_name_id());
  }

  void validate_lvalue(const Exp &exp, uint32_t line) {
    if (!std::holds_alternative<Var>(exp.value)) {
      std::println("error:{}: Expression is not a valid lvalue", line);
      had_error_ = true;
    }
  }

  void resolve_exp(Exp &exp) {
    std::visit(
        Overload{
            [](Constant &) {},
            [&](Var &v) {
              const auto it = variables_.find(v.name);
              if (it == variables_.end()) {
                std::println("error:{}: Undeclared variable '{}'", v.line,
                             v.name);
                had_error_ = true;
                return;
              }
              v.name = it->second;
            },
            [&](const Unary &u) { resolve_exp(*u.exp); },
            [&](const Binary &b) {
              resolve_exp(*b.left);
              resolve_exp(*b.right);
            },
            [&](Assignment &a) {
              validate_lvalue(*a.left, a.line);
              resolve_exp(*a.left);
              resolve_exp(*a.right);
            },
            [&](CompoundAssignment &a) {
              validate_lvalue(*a.left, a.line);
              resolve_exp(*a.left);
              resolve_exp(*a.right);
            },
            [&](IncDec &e) {
              validate_lvalue(*e.exp, e.line);
              resolve_exp(*e.exp);
            },
            [&](const Conditional &c) {
              resolve_exp(*c.condition);
              resolve_exp(*c.then_exp);
              resolve_exp(*c.else_exp);
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
            [&](If &i) {
              resolve_exp(i.condition);
              resolve_statement(*i.then_stmt);
              if (i.else_stmt) resolve_statement(*i.else_stmt);
            },
            [&](Goto &) {},
            [&](Label &l) { resolve_statement(*l.stmt); },
        },
        stmt.value);
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

class LabelResolver {
  std::unordered_map<std::string, std::string> labels_;
  std::vector<Label *> defined_labels_;
  std::vector<Goto *> gotos_;
  bool had_error_ = false;

  static std::string make_unique_name(const std::string_view name) {
    return std::string(name) + "." + std::to_string(next_name_id());
  }

  void record_statement(Statement &stmt) {
    if (auto *l = std::get_if<Label>(&stmt.value)) {
      defined_labels_.push_back(l);
      record_statement(*l->stmt);
    } else if (auto *i = std::get_if<If>(&stmt.value)) {
      record_statement(*i->then_stmt);
      if (i->else_stmt) record_statement(*i->else_stmt);
    } else if (auto *g = std::get_if<Goto>(&stmt.value)) {
      gotos_.push_back(g);
    }
  }

  void resolve_definitions() {
    for (Label *l : defined_labels_) {
      if (labels_.contains(l->name)) {
        std::println("error:{}: Duplicate label '{}'", l->line, l->name);
        had_error_ = true;
      } else {
        labels_.emplace(l->name, make_unique_name(l->name));
      }
      l->name = labels_.at(l->name);
    }
  }

  void resolve_gotos() {
    for (Goto *g : gotos_) {
      const auto it = labels_.find(g->label);
      if (it == labels_.end()) {
        std::println("error:{}: Undeclared label '{}'", g->line, g->label);
        had_error_ = true;
      } else {
        g->label = it->second;
      }
    }
  }

public:
  bool resolve(Program &program) {
    for (auto &item : program.function.body) {
      if (auto *stmt = std::get_if<Statement>(&item)) record_statement(*stmt);
    }
    resolve_definitions();
    resolve_gotos();
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

export bool resolve_labels(Program &program) {
  LabelResolver resolver;
  return resolver.resolve(program);
}
