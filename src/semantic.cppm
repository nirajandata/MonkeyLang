module;

#include <cstdint>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

export module semantic;

import ast;

namespace {
std::uint64_t name_counter = 0;
}

export std::uint64_t next_name_id();

export {
  enum class TypeKind { Int, Function };

  struct Type {
    TypeKind kind = TypeKind::Int;
    size_t param_count = 0;

    static Type int_type() { return {}; }
    static Type function(size_t param_count) {
      return {TypeKind::Function, param_count};
    }

    friend bool operator==(const Type &, const Type &) = default;
  };

  struct Symbol {
    Type type;
    struct FunAttr {
      bool defined;
      bool global;
    };
    struct InitialValue {
      enum class Kind { Tentative, Initial, NoInitializer } kind;
      int32_t value = 0;
    };
    struct StaticAttr {
      InitialValue init;
      bool global;
    };
    struct LocalAttr {};
    std::variant<FunAttr, StaticAttr, LocalAttr> attrs;
  };

  class SymbolTable {
    std::unordered_map<std::string, Symbol> symbols_;

  public:
    void clear() { symbols_.clear(); }

    void add(const std::string &name, Type type,
             std::variant<Symbol::FunAttr, Symbol::StaticAttr,
                          Symbol::LocalAttr> attrs) {
      symbols_.insert_or_assign(name, Symbol{type, std::move(attrs)});
    }

    [[nodiscard]] const Symbol *find(const std::string &name) const {
      const auto it = symbols_.find(name);
      return it == symbols_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const Symbol &get(const std::string &name) const {
      return symbols_.at(name);
    }

    [[nodiscard]] const std::unordered_map<std::string, Symbol> &entries()
        const {
      return symbols_;
    }
  };
}

export SymbolTable &symbol_table() {
  static SymbolTable table;
  return table;
}

namespace {
class IdentifierResolver {
  struct MapEntry {
    std::string new_name;
    bool from_current_scope;
    bool has_linkage;
  };

  using Scope = std::unordered_map<std::string, MapEntry>;
  using LocalNames = std::unordered_set<std::string>;
  bool had_error_ = false;

  static std::string make_unique_name(const std::string_view name) {
    return std::string(name) + "." + std::to_string(next_name_id());
  }

  static void mark_outer_scope(Scope &scope) {
    for (auto &[name, entry] : scope)
      entry.from_current_scope = false;
  }

  void validate_lvalue(const Exp &exp, uint32_t line) {
    if (!std::holds_alternative<Var>(exp.value)) {
      std::println("error:{}: Expression is not a valid lvalue", line);
      had_error_ = true;
    }
  }

  void resolve_exp(Exp &exp, const Scope &scope) {
    std::visit(Overload{
                   [](Constant &) {},
                   [&](Var &v) {
                     auto it = scope.find(v.name);
                     if (it == scope.end()) {
                       std::println("error:{}: Undeclared variable '{}'",
                                    v.line, v.name);
                       had_error_ = true;
                       return;
                     }
                      v.name = it->second.new_name;
                    },
                   [&](const Unary &u) { resolve_exp(*u.exp, scope); },
                   [&](const Binary &b) {
                     resolve_exp(*b.left, scope);
                     resolve_exp(*b.right, scope);
                   },
                   [&](Assignment &a) {
                     validate_lvalue(*a.left, a.line);
                     resolve_exp(*a.left, scope);
                     resolve_exp(*a.right, scope);
                   },
                   [&](CompoundAssignment &a) {
                     validate_lvalue(*a.left, a.line);
                     resolve_exp(*a.left, scope);
                     resolve_exp(*a.right, scope);
                   },
                   [&](IncDec &e) {
                     validate_lvalue(*e.exp, e.line);
                     resolve_exp(*e.exp, scope);
                   },
                   [&](const Conditional &c) {
                     resolve_exp(*c.condition, scope);
                     resolve_exp(*c.then_exp, scope);
                     resolve_exp(*c.else_exp, scope);
                   },
                   [&](FunctionCall &c) {
                     const auto it = scope.find(c.name);
                     if (it == scope.end()) {
                       std::println("error:{}: Call to undeclared function '{}'",
                                    c.line, c.name);
                       had_error_ = true;
                     } else {
                       c.name = it->second.new_name;
                     }
                     for (auto &arg : c.args) resolve_exp(*arg, scope);
                   },
               },
               exp.value);
  }

  void resolve_statement(Statement &stmt, Scope &scope,
                         LocalNames &local_names) {
    std::visit(
        Overload{
            [&](Return &r) { resolve_exp(r.value, scope); },
            [&](Expression &e) { resolve_exp(e.value, scope); },
            [&](Null &) {},
            [&](If &i) {
              resolve_exp(i.condition, scope);
              resolve_statement(*i.then_stmt, scope, local_names);
              if (i.else_stmt)
                resolve_statement(*i.else_stmt, scope, local_names);
            },
            [&](Goto &) {},
            [&](Label &l) { resolve_statement(*l.stmt, scope, local_names); },
            [&](Compound &c) { resolve_block(*c.block, scope); },
            [&](Break &) {},
            [&](Continue &) {},
            [&](Case &c) {
              resolve_exp(c.value, scope);
              resolve_statement(*c.stmt, scope, local_names);
            },
            [&](Default &d) { resolve_statement(*d.stmt, scope, local_names); },
            [&](Switch &s) {
              resolve_exp(s.condition, scope);
              resolve_statement(*s.body, scope, local_names);
            },
            [&](While &w) {
              resolve_exp(w.condition->value, scope);
              resolve_statement(*w.body, scope, local_names);
            },
            [&](DoWhile &d) {
              resolve_statement(*d.body, scope, local_names);
              resolve_exp(d.condition->value, scope);
            },
            [&](For &f) {
              Scope loop_scope = scope;
              mark_outer_scope(loop_scope);
              LocalNames loop_names;
              resolve_for_init(f.init, loop_scope, loop_names);
              if (f.condition)
                resolve_exp(f.condition->value, loop_scope);
              if (f.post)
                resolve_exp(f.post->value, loop_scope);
              resolve_statement(*f.body, loop_scope, loop_names);
            },
        },
        stmt.value);
  }

  void resolve_local_declaration(std::string &name, uint32_t line,
                                 Scope &scope, LocalNames &local_names) {
    if (local_names.contains(name)) {
      std::println("error:{}: Duplicate declaration '{}'", line, name);
      had_error_ = true;
      return;
    }
    auto new_name = make_unique_name(name);
    local_names.insert(name);
    scope.insert_or_assign(name, MapEntry{new_name, true, false});
    name = std::move(new_name);
  }

  void resolve_variable_declaration(VariableDeclaration &d, Scope &scope,
                                    LocalNames &local_names) {
    const bool has_linkage =
        d.storage_class && std::holds_alternative<Extern>(*d.storage_class);
    const auto prev = scope.find(d.name);
    const bool declared_in_scope =
        prev != scope.end() && prev->second.from_current_scope;
    if (declared_in_scope &&
        (!prev->second.has_linkage || !has_linkage)) {
      std::println("error:{}: Conflicting local declarations of '{}'",
                   d.line, d.name);
      had_error_ = true;
    }

    if (has_linkage) {
      scope.insert_or_assign(d.name, MapEntry{d.name, true, true});
      local_names.insert(d.name);
      if (d.init) {
        std::println("error:{}: Initializer on local extern variable '{}'",
                     d.line, d.name);
        had_error_ = true;
      }
      return;
    }

    if (declared_in_scope)
      return;
    resolve_local_declaration(d.name, d.line, scope, local_names);
    if (d.init)
      resolve_exp(*d.init, scope);
  }

  void resolve_file_scope_variable_declaration(VariableDeclaration &d,
                                                Scope &scope) {
    scope.insert_or_assign(d.name, MapEntry{d.name, true, true});
  }

  void resolve_function_declaration(FunctionDeclaration &d, Scope &scope,
                                    LocalNames &local_names) {
    const auto it = scope.find(d.name);
    if (it != scope.end() && local_names.contains(d.name) &&
        !it->second.has_linkage) {
      std::println("error:{}: Duplicate declaration '{}'", d.line, d.name);
      had_error_ = true;
    }
    local_names.insert(d.name);
    scope.insert_or_assign(d.name, MapEntry{d.name, true, true});

    Scope inner = scope;
    mark_outer_scope(inner);
    LocalNames inner_names;
    for (auto &param : d.params)
      resolve_local_declaration(param, d.line, inner, inner_names);
    if (d.body)
      resolve_block_items(*d.body, inner, inner_names);
  }

  void resolve_declaration(Declaration &d, Scope &scope,
                           LocalNames &local_names) {
    std::visit(
        Overload{
            [&](FunDecl &f) {
              if (f.decl.body) {
                std::println(
                    "error:{}: Nested function definitions are not supported",
                    f.decl.line);
                had_error_ = true;
              } else {
                if (f.decl.storage_class &&
                    std::holds_alternative<Static>(*f.decl.storage_class)) {
                  std::println("error:{}: Block-scope function declaration cannot be static",
                               f.decl.line);
                  had_error_ = true;
                }
                resolve_function_declaration(f.decl, scope, local_names);
              }
            },
            [&](VarDecl &v) {
              resolve_variable_declaration(v.decl, scope, local_names);
            },
        },
        d);
  }

  void resolve_for_init(ForInit &init, Scope &scope, LocalNames &local_names) {
    std::visit(Overload{
                   [&](InitDecl &d) {
                     resolve_variable_declaration(d.decl, scope, local_names);
                   },
                   [&](InitExp &e) {
                     if (e.exp)
                       resolve_exp(*e.exp, scope);
                   },
               },
               init);
  }

  void resolve_block_item(BlockItem &item, Scope &scope,
                          LocalNames &local_names) {
    std::visit(
        Overload{
            [&](Statement &stmt) {
              resolve_statement(stmt, scope, local_names);
            },
            [&](Declaration &d) { resolve_declaration(d, scope, local_names); },
        },
        item);
  }

  void resolve_block(Block &block, Scope scope) {
    mark_outer_scope(scope);
    LocalNames local_names;
    resolve_block_items(block, scope, local_names);
  }

  void resolve_block_items(Block &block, Scope &scope,
                           LocalNames &local_names) {
    for (auto &item : block.items)
      resolve_block_item(item, scope, local_names);
  }

public:
  bool resolve(Program &program) {
    Scope scope;
    LocalNames local_names;
    for (auto &declaration : program.declarations) {
      if (auto *func = std::get_if<FunDecl>(&declaration)) {
        resolve_function_declaration(func->decl, scope, local_names);
      } else {
        resolve_file_scope_variable_declaration(
            std::get<VarDecl>(declaration).decl, scope);
      }
    }

    return !had_error_;
  }
};

class TypeChecker {
  bool had_error_ = false;
  using InitialValue = Symbol::InitialValue;

  static bool is_extern(const VariableDeclaration &d) {
    return d.storage_class &&
           std::holds_alternative<Extern>(*d.storage_class);
  }

  static bool is_static(const VariableDeclaration &d) {
    return d.storage_class &&
           std::holds_alternative<Static>(*d.storage_class);
  }

  std::optional<int32_t> constant_initializer(const VariableDeclaration &d) {
    if (!d.init) return std::nullopt;
    if (const auto *constant = std::get_if<Constant>(&d.init->value))
      return constant->value;
    return std::nullopt;
  }

  void typecheck_exp(const Exp &exp) {
    std::visit(
        Overload{
            [](const Constant &) {},
            [&](const Var &v) {
              const Type &var_type = symbol_table().get(v.name).type;
              if (var_type.kind == TypeKind::Function) {
                std::println("error:{}: Function name used as variable",
                             v.line);
                had_error_ = true;
              }
            },
            [&](const Unary &u) { typecheck_exp(*u.exp); },
            [&](const Binary &b) {
              typecheck_exp(*b.left);
              typecheck_exp(*b.right);
            },
            [&](const Assignment &a) {
              typecheck_exp(*a.left);
              typecheck_exp(*a.right);
            },
            [&](const CompoundAssignment &a) {
              typecheck_exp(*a.left);
              typecheck_exp(*a.right);
            },
            [&](const IncDec &e) { typecheck_exp(*e.exp); },
            [&](const Conditional &c) {
              typecheck_exp(*c.condition);
              typecheck_exp(*c.then_exp);
              typecheck_exp(*c.else_exp);
            },
            [&](const FunctionCall &c) {
              const Type &fun_type = symbol_table().get(c.name).type;
              if (fun_type.kind == TypeKind::Int) {
                std::println("error:{}: Variable used as function name",
                             c.line);
                had_error_ = true;
              } else if (fun_type.param_count != c.args.size()) {
                std::println(
                    "error:{}: Function called with the wrong number of "
                    "arguments",
                    c.line);
                had_error_ = true;
              }
              for (const auto &arg : c.args)
                typecheck_exp(*arg);
            },
        },
        exp.value);
  }

  void typecheck_statement(const Statement &stmt) {
    std::visit(
        Overload{
            [&](const Return &r) { typecheck_exp(r.value); },
            [&](const Expression &e) { typecheck_exp(e.value); },
            [](const Null &) {},
            [&](const If &i) {
              typecheck_exp(i.condition);
              typecheck_statement(*i.then_stmt);
              if (i.else_stmt)
                typecheck_statement(*i.else_stmt);
            },
            [](const Goto &) {},
            [&](const Label &l) { typecheck_statement(*l.stmt); },
            [&](const Compound &c) { typecheck_block(*c.block); },
            [](const Break &) {},
            [](const Continue &) {},
            [&](const Case &c) {
              typecheck_exp(c.value);
              typecheck_statement(*c.stmt);
            },
            [&](const Default &d) { typecheck_statement(*d.stmt); },
            [&](const Switch &s) {
              typecheck_exp(s.condition);
              typecheck_statement(*s.body);
            },
            [&](const While &w) {
              typecheck_exp(w.condition->value);
              typecheck_statement(*w.body);
            },
            [&](const DoWhile &d) {
              typecheck_statement(*d.body);
              typecheck_exp(d.condition->value);
            },
            [&](const For &f) {
              typecheck_for_init(f.init);
              if (f.condition)
                typecheck_exp(f.condition->value);
              if (f.post)
                typecheck_exp(f.post->value);
              typecheck_statement(*f.body);
            },
        },
        stmt.value);
  }

  void typecheck_local_variable_declaration(const VariableDeclaration &d) {
    if (is_extern(d)) {
      if (d.init) {
        std::println("error:{}: Initializer on local extern variable '{}'",
                     d.line, d.name);
        had_error_ = true;
      }
      if (const Symbol *prev = symbol_table().find(d.name)) {
        if (prev->type.kind == TypeKind::Function) {
          std::println("error:{}: Function redeclared as variable", d.line);
          had_error_ = true;
        }
      } else {
        symbol_table().add(d.name, Type::int_type(),
                           Symbol::StaticAttr{
                               {InitialValue::Kind::NoInitializer, 0}, true});
      }
      return;
    }

    if (is_static(d)) {
      InitialValue init{InitialValue::Kind::Initial, 0};
      if (d.init) {
        if (auto value = constant_initializer(d)) {
          init.value = *value;
        } else {
          std::println("error:{}: Non-constant initializer on local static variable '{}'",
                       d.line, d.name);
          had_error_ = true;
        }
      }
      symbol_table().add(d.name, Type::int_type(),
                         Symbol::StaticAttr{init, false});
      return;
    }

    symbol_table().add(d.name, Type::int_type(), Symbol::LocalAttr{});
    if (d.init)
      typecheck_exp(*d.init);
  }

  void typecheck_file_scope_variable_declaration(
      const VariableDeclaration &d) {
    InitialValue init{InitialValue::Kind::Tentative, 0};
    if (d.init) {
      if (auto value = constant_initializer(d)) {
        init = {InitialValue::Kind::Initial, *value};
      } else {
        std::println("error:{}: Non-constant initializer for file-scope variable '{}'",
                     d.line, d.name);
        had_error_ = true;
        init = {InitialValue::Kind::Tentative, 0};
      }
    } else if (is_extern(d)) {
      init = {InitialValue::Kind::NoInitializer, 0};
    }

    bool global = !is_static(d);
    if (const Symbol *prev = symbol_table().find(d.name)) {
      if (prev->type.kind != TypeKind::Int) {
        std::println("error:{}: Function redeclared as variable", d.line);
        had_error_ = true;
        return;
      }

      const auto &old = std::get<Symbol::StaticAttr>(prev->attrs);
      if (is_extern(d)) {
        global = old.global;
      } else if (old.global != global) {
        std::println("error:{}: Conflicting variable linkage for '{}'",
                     d.line, d.name);
        had_error_ = true;
      }

      if (old.init.kind == InitialValue::Kind::Initial) {
        if (init.kind == InitialValue::Kind::Initial) {
          std::println("error:{}: Conflicting file-scope variable definitions",
                       d.line);
          had_error_ = true;
        } else {
          init = old.init;
        }
      } else if (init.kind != InitialValue::Kind::Initial &&
                 old.init.kind == InitialValue::Kind::Tentative) {
        init = old.init;
      }
    }

    symbol_table().add(d.name, Type::int_type(),
                       Symbol::StaticAttr{init, global});
  }

  void typecheck_function_declaration(const FunctionDeclaration &d) {
    const Type fun_type = Type::function(d.params.size());
    const bool has_body = static_cast<bool>(d.body);
    bool already_defined = false;
    bool global = !d.storage_class ||
                  !std::holds_alternative<Static>(*d.storage_class);

    if (const Symbol *prev = symbol_table().find(d.name)) {
      if (!(prev->type == fun_type)) {
        std::println("error:{}: Incompatible function declarations", d.line);
        had_error_ = true;
      } else if (const auto *old = std::get_if<Symbol::FunAttr>(&prev->attrs)) {
        already_defined = old->defined;
        if (old->global && d.storage_class &&
            std::holds_alternative<Static>(*d.storage_class)) {
          std::println("error:{}: Static function declaration follows non-static",
                       d.line);
          had_error_ = true;
        }
        global = old->global;
      } else {
        std::println("error:{}: Variable redeclared as function", d.line);
        had_error_ = true;
      }
      if (already_defined && has_body) {
        std::println("error:{}: Function is defined more than once", d.line);
        had_error_ = true;
      }
    }

    symbol_table().add(d.name, fun_type,
                       Symbol::FunAttr{already_defined || has_body, global});

    if (!has_body)
      return;
    for (const auto &param : d.params)
      symbol_table().add(param, Type::int_type(), Symbol::LocalAttr{});
    typecheck_block(*d.body);
  }

  void typecheck_declaration(const Declaration &d) {
    std::visit(
        Overload{
            [&](const FunDecl &f) { typecheck_function_declaration(f.decl); },
            [&](const VarDecl &v) {
            typecheck_local_variable_declaration(v.decl);
            },
        },
        d);
  }

  void typecheck_for_init(const ForInit &init) {
    std::visit(
        Overload{
            [&](const InitDecl &d) {
              if (d.decl.storage_class) {
                std::println("error:{}: Storage-class specifier is not allowed in a for initializer",
                             d.decl.line);
                had_error_ = true;
              }
              typecheck_local_variable_declaration(d.decl);
            },
            [&](const InitExp &e) {
              if (e.exp)
                typecheck_exp(*e.exp);
            },
        },
        init);
  }

  void typecheck_block(const Block &block) {
    for (const auto &item : block.items) {
      std::visit(
          Overload{
              [&](const Statement &stmt) { typecheck_statement(stmt); },
              [&](const Declaration &d) { typecheck_declaration(d); },
          },
          item);
    }
  }

public:
  bool check(Program &program) {
    symbol_table().clear();

    for (const auto &declaration : program.declarations) {
      if (const auto *func = std::get_if<FunDecl>(&declaration)) {
        typecheck_function_declaration(func->decl);
      } else {
        typecheck_file_scope_variable_declaration(
            std::get<VarDecl>(declaration).decl);
      }
    }

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
      if (i->else_stmt)
        record_statement(*i->else_stmt);
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
    } else if (auto *s = std::get_if<Switch>(&stmt.value)) {
      record_statement(*s->body);
    } else if (auto *c = std::get_if<Case>(&stmt.value)) {
      record_statement(*c->stmt);
    } else if (auto *d = std::get_if<Default>(&stmt.value)) {
      record_statement(*d->stmt);
    }
  }

  void record_block(Block &block) {
    for (auto &item : block.items) {
      if (auto *stmt = std::get_if<Statement>(&item))
        record_statement(*stmt);
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
    for (auto &declaration : program.declarations) {
      if (auto *func = std::get_if<FunDecl>(&declaration);
          func && func->decl.body)
        record_block(*func->decl.body);
    }
    resolve_definitions();
    resolve_gotos();
    return !had_error_;
  }
};

class BreakContinueLabeler {
  struct Targets {
    std::string brk_label;
    const std::string *cont_label;
  };

  bool had_error_ = false;

  static std::string make_unique_name(const std::string_view name) {
    return std::string(name) + "." + std::to_string(next_name_id());
  }

  void find_enclosing_loop(Exp &exp, const Targets *loop) {
    std::visit(Overload{
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
                  [&](FunctionCall &c) {
                    for (auto &arg : c.args) find_enclosing_loop(*arg, loop);
                  },
               },
               exp.value);
  }

  void find_enclosing_loop(ForInit &init, const Targets *loop) {
    std::visit(Overload{
                   [&](InitDecl &d) {
                     if (d.decl.init)
                       find_enclosing_loop(*d.decl.init, loop);
                   },
                   [&](InitExp &e) {
                     if (e.exp)
                       find_enclosing_loop(*e.exp, loop);
                   },
               },
               init);
  }

  template <typename Loop>
  void assign_loop_labels(Loop &l, const Targets *loop) {
    l.break_label = make_unique_name("break");
    l.continue_label = make_unique_name("continue");
    Targets current{l.break_label, &l.continue_label};
    if constexpr (requires { l.init; })
      find_enclosing_loop(l.init, loop);
    if constexpr (requires { l.condition->value; })
      find_enclosing_loop(l.condition->value, loop);
    else if (l.condition)
      find_enclosing_loop(l.condition->value, loop);
    if constexpr (requires { l.post; }) {
      if (l.post)
        find_enclosing_loop(l.post->value, loop);
    }
    record_break_and_continue_labels(*l.body, &current);
  }

  void assign_switch_labels(Switch &s, const Targets *loop) {
    s.break_label = make_unique_name("break");
    find_enclosing_loop(s.condition, loop);
    Targets current{s.break_label, loop ? loop->cont_label : nullptr};
    record_break_and_continue_labels(*s.body, &current);
  }

  void record_break_and_continue_labels(Statement &stmt, const Targets *loop) {
    std::visit(
        Overload{
            [&](Break &b) {
              if (loop) {
                b.label = loop->brk_label;
              } else {
                std::println(
                    "error:{}: 'break' statement not inside loop or switch",
                    b.line);
                had_error_ = true;
              }
            },
            [&](Continue &c) {
              if (loop && loop->cont_label) {
                c.label = *loop->cont_label;
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
              if (i.else_stmt)
                record_break_and_continue_labels(*i.else_stmt, loop);
            },
            [&](Compound &c) { record_block(*c.block, loop); },
            [&](Case &c) { record_break_and_continue_labels(*c.stmt, loop); },
            [&](Default &d) {
              record_break_and_continue_labels(*d.stmt, loop);
            },
            [&](Switch &s) { assign_switch_labels(s, loop); },
            [&](While &w) { assign_loop_labels(w, loop); },
            [&](DoWhile &d) { assign_loop_labels(d, loop); },
            [&](For &f) { assign_loop_labels(f, loop); },
        },
        stmt.value);
  }

  void record_block(Block &block, const Targets *loop) {
    for (auto &item : block.items) {
      if (auto *stmt = std::get_if<Statement>(&item)) {
        record_break_and_continue_labels(*stmt, loop);
      }
    }
  }

public:
  bool resolve(Program &program) {
    for (auto &declaration : program.declarations) {
      if (auto *func = std::get_if<FunDecl>(&declaration);
          func && func->decl.body)
        record_block(*func->decl.body, nullptr);
    }
    return !had_error_;
  }
};

class SwitchGatherer {
  struct SwitchCases {
    std::vector<Case *> case_list;
    std::unordered_set<int32_t> values;
    Default *default_case = nullptr;
  };

  bool had_error_ = false;

  static std::string make_unique_name(const std::string_view name) {
    return std::string(name) + "." + std::to_string(next_name_id());
  }

  void record_case(Case &c, SwitchCases &cases) {
    const auto *value = std::get_if<Constant>(&c.value.value);
    if (!value) {
      std::println("error:{}: Case value must be a constant expression",
                   c.line);
      had_error_ = true;
      return;
    }
    if (!cases.values.insert(value->value).second) {
      std::println("error:{}: Duplicate case value '{}'", c.line, value->value);
      had_error_ = true;
      return;
    }
    cases.case_list.push_back(&c);
    c.label = make_unique_name("case");
  }

  void record_default(Default &d, SwitchCases &cases) {
    if (cases.default_case) {
      std::println("error:{}: Duplicate default statement", d.line);
      had_error_ = true;
      return;
    }
    d.label = make_unique_name("default");
    cases.default_case = &d;
  }

  void record_statement(Statement &stmt, SwitchCases *cases) {
    std::visit(
        Overload{
            [&](Break &) {},
            [&](Continue &) {},
            [&](Return &) {},
            [&](Expression &) {},
            [&](Null &) {},
            [&](Goto &) {},
            [&](Label &l) { record_statement(*l.stmt, cases); },
            [&](If &i) {
              record_statement(*i.then_stmt, cases);
              if (i.else_stmt)
                record_statement(*i.else_stmt, cases);
            },
            [&](Compound &c) { record_block(*c.block, cases); },
            [&](Case &c) {
              if (cases) {
                record_case(c, *cases);
                record_statement(*c.stmt, cases);
              } else {
                std::println(
                    "error:{}: 'case' statement not inside switch statement",
                    c.line);
                had_error_ = true;
              }
            },
            [&](Default &d) {
              if (cases) {
                record_default(d, *cases);
                record_statement(*d.stmt, cases);
              } else {
                std::println(
                    "error:{}: 'default' statement not inside switch statement",
                    d.line);
                had_error_ = true;
              }
            },
            [&](While &w) { record_statement(*w.body, cases); },
            [&](DoWhile &d) { record_statement(*d.body, cases); },
            [&](For &f) { record_statement(*f.body, cases); },
            [&](Switch &s) { gather(s); },
        },
        stmt.value);
  }

  void record_block(Block &block, SwitchCases *cases) {
    for (auto &item : block.items) {
      if (auto *stmt = std::get_if<Statement>(&item)) {
        record_statement(*stmt, cases);
      }
    }
  }

  void gather(Switch &s) {
    SwitchCases cases;
    record_statement(*s.body, &cases);
    s.cases = std::move(cases.case_list);
    s.default_case = cases.default_case;
  }

public:
  bool resolve(Program &program) {
    for (auto &declaration : program.declarations) {
      if (auto *func = std::get_if<FunDecl>(&declaration);
          func && func->decl.body)
        record_block(*func->decl.body, nullptr);
    }
    return !had_error_;
  }
};
} // namespace

export std::uint64_t next_name_id() { return name_counter++; }

export bool resolve_identifiers(Program &program) {
  IdentifierResolver resolver;
  return resolver.resolve(program);
}

export bool typecheck(Program &program) {
  TypeChecker checker;
  return checker.check(program);
}

export bool resolve_labels(Program &program) {
  LabelResolver resolver;
  return resolver.resolve(program);
}

export bool resolve_break_and_continue(Program &program) {
  BreakContinueLabeler labeler;
  return labeler.resolve(program);
}

export bool resolve_switches(Program &program) {
  SwitchGatherer gatherer;
  return gatherer.resolve(program);
}