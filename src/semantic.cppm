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
  std::vector<std::unordered_map<std::string, std::string>> scopes_;
  bool had_error_ = false;

  static std::string make_unique_name(const std::string_view name) {
    return std::string(name) + "." + std::to_string(next_name_id());
  }

  void enter_scope() { scopes_.emplace_back(); }

  void leave_scope() { scopes_.pop_back(); }

  const std::string *find_variable(const std::string &name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      const auto it = scope->find(name);
      if (it != scope->end()) return &it->second;
    }
    return nullptr;
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
              const std::string *unique_name = find_variable(v.name);
              if (!unique_name) {
                std::println("error:{}: Undeclared variable '{}'", v.line,
                             v.name);
                had_error_ = true;
                return;
              }
              v.name = *unique_name;
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
            [&](Compound &c) { resolve_block(*c.block); },
            [&](Break &) {},
            [&](Continue &) {},
            [&](While &w) {
              resolve_exp(w.condition->value);
              resolve_statement(*w.body);
            },
            [&](DoWhile &d) {
              resolve_statement(*d.body);
              resolve_exp(d.condition->value);
            },
            [&](For &f) {
              resolve_for_init(f.init);
              if (f.condition) resolve_exp(f.condition->value);
              if (f.post) resolve_exp(f.post->value);
              resolve_statement(*f.body);
            },
        },
        stmt.value);
  }

  void resolve_declaration(Declaration &d) {
    if (scopes_.back().contains(d.name)) {
      std::println("error:{}: Duplicate variable '{}'", d.line, d.name);
      had_error_ = true;
    } else {
      auto unique_name = make_unique_name(d.name);
      scopes_.back().emplace(d.name, unique_name);
      d.name = std::move(unique_name);
    }
    if (d.init) resolve_exp(*d.init);
  }

  void resolve_for_init(ForInit &init) {
    std::visit(
        Overload{
            [&](InitDecl &d) { resolve_declaration(d.decl); },
            [&](InitExp &e) {
              if (e.exp) resolve_exp(*e.exp);
            },
        },
        init);
  }

  void resolve_block_item(BlockItem &item) {
    std::visit(
        Overload{
            [&](Statement &stmt) { resolve_statement(stmt); },
            [&](Declaration &d) { resolve_declaration(d); },
        },
        item);
  }

  void resolve_block(Block &block) {
    enter_scope();
    for (auto &item : block.items) resolve_block_item(item);
    leave_scope();
  }

public:
  bool resolve(Program &program) {
    resolve_block(program.function.body);
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
    } else if (auto *c = std::get_if<Compound>(&stmt.value)) {
      record_block(*c->block);
    } else if (auto *g = std::get_if<Goto>(&stmt.value)) {
      gotos_.push_back(g);
    } else if (auto *w = std::get_if<While>(&stmt.value)) {
      record_statement(*w->body);
    } else if (auto *d = std::get_if<DoWhile>(&stmt.value)) {
      record_statement(*d->body);
    } else if (auto *f = std::get_if<For>(&stmt.value)) {
      record_statement(*f->body);
    }
  }

  void record_block(Block &block) {
    for (auto &item : block.items) {
      if (auto *stmt = std::get_if<Statement>(&item)) record_statement(*stmt);
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
    record_block(program.function.body);
    resolve_definitions();
    resolve_gotos();
    return !had_error_;
  }
};

class LoopLabeler {
  struct LoopLabels {
    std::string brk_label;
    std::string cont_label;
  };

  bool had_error_ = false;

  static std::string make_unique_name(const std::string_view name) {
    return std::string(name) + "." + std::to_string(next_name_id());
  }

  void find_enclosing_loop(Exp &exp, const LoopLabels *loop) {
    std::visit(
        Overload{
            [](Constant &) {},
            [](Var &) {},
            [&](Unary &u) { find_enclosing_loop(*u.exp, loop); },
            [&](Binary &b) {
              find_enclosing_loop(*b.left, loop);
              find_enclosing_loop(*b.right, loop);
            },
            [&](Assignment &a) {
              find_enclosing_loop(*a.left, loop);
              find_enclosing_loop(*a.right, loop);
            },
            [&](CompoundAssignment &a) {
              find_enclosing_loop(*a.left, loop);
              find_enclosing_loop(*a.right, loop);
            },
            [&](IncDec &e) { find_enclosing_loop(*e.exp, loop); },
            [&](Conditional &c) {
              find_enclosing_loop(*c.condition, loop);
              find_enclosing_loop(*c.then_exp, loop);
              find_enclosing_loop(*c.else_exp, loop);
            },
        },
        exp.value);
  }

  void find_enclosing_loop(ForInit &init, const LoopLabels *loop) {
    std::visit(
        Overload{
            [&](InitDecl &d) {
              if (d.decl.init) find_enclosing_loop(*d.decl.init, loop);
            },
            [&](InitExp &e) {
              if (e.exp) find_enclosing_loop(*e.exp, loop);
            },
        },
        init);
  }

  template <typename Loop>
  void assign_loop_labels(Loop &l, const LoopLabels *loop) {
    LoopLabels current{make_unique_name("break"),
                       make_unique_name("continue")};
    l.break_label = current.brk_label;
    l.continue_label = current.cont_label;
    if constexpr (requires { l.init; }) find_enclosing_loop(l.init, loop);
    find_enclosing_loop(l.condition->value, loop);
    if constexpr (requires { l.post; }) {
      if (l.post) find_enclosing_loop(l.post->value, loop);
    }
    record_break_and_continue_labels(*l.body, &current);
  }

  void record_break_and_continue_labels(Statement &stmt,
                                        const LoopLabels *loop) {
    std::visit(
        Overload{
            [&](Break &b) {
              if (loop) {
                b.label = loop->brk_label;
              } else {
                std::println("error:{}: 'break' statement not inside loop",
                             b.line);
                had_error_ = true;
              }
            },
            [&](Continue &c) {
              if (loop) {
                c.label = loop->cont_label;
              } else {
                std::println("error:{}: 'continue' statement not inside loop",
                             c.line);
                had_error_ = true;
              }
            },
            [&](Return &) {},
            [&](Expression &) {},
            [&](Null &) {},
            [&](Goto &) {},
            [&](Label &l) { record_break_and_continue_labels(*l.stmt, loop); },
            [&](If &i) {
              find_enclosing_loop(i.condition, loop);
              record_break_and_continue_labels(*i.then_stmt, loop);
              if (i.else_stmt) record_break_and_continue_labels(*i.else_stmt, loop);
            },
            [&](Compound &c) { record_block(*c.block, loop); },
            [&](While &w) { assign_loop_labels(w, loop); },
            [&](DoWhile &d) { assign_loop_labels(d, loop); },
            [&](For &f) { assign_loop_labels(f, loop); },
        },
        stmt.value);
  }

  void record_block(Block &block, const LoopLabels *loop) {
    for (auto &item : block.items) {
      if (auto *stmt = std::get_if<Statement>(&item)) {
        record_break_and_continue_labels(*stmt, loop);
      }
    }
  }

public:
  bool resolve(Program &program) {
    record_block(program.function.body, nullptr);
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

export bool resolve_loops(Program &program) {
  LoopLabeler labeler;
  return labeler.resolve(program);
}
