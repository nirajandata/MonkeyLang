module;

#include <cstdint>
#include <bit>
#include <cmath>
#include <memory>
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
  struct Symbol {
    Type type;
    struct FunAttr {
      bool defined;
      bool global;
    };
    struct InitialValue {
      enum class Kind { Tentative, Initial, NoInitializer } kind;
      uint64_t value = 0;
      double double_value = 0.0;
      bool is_double = false;
      std::vector<StaticInit> values;
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

  void resolve_exp(Exp &exp, const Scope &scope) {
    std::visit(Overload{
                   [](ConstInt &) {},
                   [](ConstLong &) {},
                   [](ConstUInt &) {},
                   [](ConstULong &) {},
                   [](ConstDouble &) {},
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
                   [&](Dereference &d) { resolve_exp(*d.exp, scope); },
                   [&](AddrOf &a) { resolve_exp(*a.exp, scope); },
                   [&](Cast &c) { resolve_exp(*c.exp, scope); },
                   [&](Binary &b) {
                     resolve_exp(*b.left, scope);
                     resolve_exp(*b.right, scope);
                   },
                   [&](Assignment &a) {
                     resolve_exp(*a.left, scope);
                     resolve_exp(*a.right, scope);
                   },
                   [&](CompoundAssignment &a) {
                     resolve_exp(*a.left, scope);
                     resolve_exp(*a.right, scope);
                   },
                   [&](IncDec &e) {
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
                   [&](Subscript &s) {
                     resolve_exp(*s.left, scope);
                     resolve_exp(*s.right, scope);
                   },
                   [](String &) {},
               },
               exp.value);
  }

  void resolve_initializer(Initializer &init, const Scope &scope) {
    std::visit(
        Overload{
            [&](SingleInit &single) { resolve_exp(single.exp, scope); },
            [&](CompoundInit &compound) {
              for (auto &child : compound.initializers)
                resolve_initializer(child, scope);
            }},
        init.value);
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
      resolve_initializer(*d.init, scope);
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
  Type return_type_ = Type::int_type();

  static bool is_extern(const VariableDeclaration &d) {
    return d.storage_class &&
           std::holds_alternative<Extern>(*d.storage_class);
  }

  static bool is_static(const VariableDeclaration &d) {
    return d.storage_class &&
           std::holds_alternative<Static>(*d.storage_class);
  }

  static Initializer zero_initializer(const Type &type) {
    if (type.kind == TypeKind::Array && type.referenced) {
      std::vector<Initializer> elements;
      elements.reserve(type.size);
      for (size_t i = 0; i < type.size; ++i)
        elements.push_back(zero_initializer(*type.referenced));
      return Initializer{CompoundInit{std::move(elements)}, type};
    }
    if (is_char_kind(type.kind)) {
      Exp zero{ConstInt{0, 0}, Type::int_type()};
      return Initializer{SingleInit{convert_to(std::move(zero), type)}, type};
    }
    Exp zero = type.kind == TypeKind::Double
                   ? Exp{ConstDouble{0.0, 0}, type}
                   : type.kind == TypeKind::Long
                         ? Exp{ConstLong{0, 0}, type}
                         : type.kind == TypeKind::UInt
                               ? Exp{ConstUInt{0, 0}, type}
                               : type.kind == TypeKind::ULong ||
                                         type.kind == TypeKind::Pointer
                                     ? Exp{ConstULong{0, 0}, type}
                                     : Exp{ConstInt{0, 0}, type};
    return Initializer{SingleInit{std::move(zero)}, type};
  }

  Type typecheck_initializer(const Type &target, Initializer &init) {
    if (target.kind == TypeKind::Array && target.referenced &&
        is_char_kind(target.referenced->kind) &&
        std::holds_alternative<SingleInit>(init.value)) {
      SingleInit &single = std::get<SingleInit>(init.value);
      if (auto *str = std::get_if<String>(&single.exp.value)) {
        const std::string value = str->value;
        const uint32_t line = str->line;
        const size_t n = value.size();
        if (target.size < n) {
          std::println("error:{}: String literal is too long for array", line);
          had_error_ = true;
          init.type = target;
          return target;
        }
        std::vector<Initializer> elements;
        elements.reserve(target.size);
        for (size_t i = 0; i < target.size; ++i) {
          const int c =
              i < n ? static_cast<int>(static_cast<unsigned char>(value[i]))
                    : 0;
          elements.push_back(Initializer{
              SingleInit{Exp{ConstInt{c, line}, Type::int_type()}}, {}});
        }
        init = Initializer{CompoundInit{std::move(elements)}, target};
        auto &stored = std::get<CompoundInit>(init.value);
        for (auto &child : stored.initializers)
          typecheck_initializer(*target.referenced, child);
        return target;
      }
    }
    return std::visit(
        Overload{
            [&](SingleInit &single) {
              if (target.kind == TypeKind::Array) {
                std::println("error: Cannot initialize an array with a scalar initializer");
                had_error_ = true;
              } else {
                typecheck_and_convert(single.exp);
                convert_by_assignment(single.exp, target);
              }
              init.type = target;
              return target;
            },
            [&](CompoundInit &compound) {
              if (target.kind != TypeKind::Array || !target.referenced) {
                std::println("error: Compound initializer requires an array type");
                had_error_ = true;
                init.type = target;
                return target;
              }
              if (compound.initializers.size() > target.size) {
                std::println("error: Too many values in array initializer");
                had_error_ = true;
              }
              for (auto &child : compound.initializers)
                typecheck_initializer(*target.referenced, child);
              while (compound.initializers.size() < target.size)
                compound.initializers.push_back(
                    zero_initializer(*target.referenced));
              init.type = target;
              return target;
            }},
        init.value);
  }

  std::optional<std::variant<uint64_t, double>>
  constant_initializer(const Exp &initializer, const Type &target) {
    auto evaluate = [&](const auto &self, const Exp &exp)
        -> std::optional<long double> {
      return std::visit(
          Overload{
              [](const ConstInt &constant) -> std::optional<long double> {
                return constant.value;
              },
              [](const ConstLong &constant) -> std::optional<long double> {
                return static_cast<long double>(constant.value);
              },
              [](const ConstUInt &constant) -> std::optional<long double> {
                return constant.value;
              },
              [](const ConstULong &constant) -> std::optional<long double> {
                return static_cast<long double>(constant.value);
              },
              [](const ConstDouble &constant) -> std::optional<long double> {
                return constant.value;
              },
              [&](const Cast &cast) -> std::optional<long double> {
                auto value = self(self, *cast.exp);
                if (!value) return std::nullopt;
                if (cast.target_type.kind == TypeKind::Double)
                  return static_cast<double>(*value);
                if (cast.target_type.kind == TypeKind::Int ||
                    cast.target_type.kind == TypeKind::UInt ||
                    cast.target_type.kind == TypeKind::Long ||
                    cast.target_type.kind == TypeKind::ULong) {
                  if (cast.exp->type.kind == TypeKind::Double) {
                    const bool is_unsigned =
                        cast.target_type.kind == TypeKind::UInt ||
                        cast.target_type.kind == TypeKind::ULong;
                    const int bits =
                        cast.target_type.kind == TypeKind::Int ||
                                cast.target_type.kind == TypeKind::UInt
                            ? 32
                            : 64;
                    if (!std::isfinite(*value) ||
                        *value < (is_unsigned ? 0.0L
                                              : -std::ldexp(1.0L, bits - 1)) ||
                        *value >= (is_unsigned ? std::ldexp(1.0L, bits)
                                               : std::ldexp(1.0L, bits - 1)))
                      return std::nullopt;
                  }
                  return std::trunc(*value);
                }
                return value;
              },
              [](const auto &) -> std::optional<long double> {
                return std::nullopt;
              }},
          exp.value);
    };
    auto value = evaluate(evaluate, initializer);
    if (!value) return std::nullopt;
    if (target.kind == TypeKind::Pointer) {
      if (*value == 0)
        return std::variant<uint64_t, double>{uint64_t{0}};
      return std::nullopt;
    }
    if (target.kind == TypeKind::Double)
      return std::variant<uint64_t, double>{static_cast<double>(*value)};
    if (!std::isfinite(*value)) return std::nullopt;

    const bool double_source = initializer.type.kind == TypeKind::Double;
    const bool unsigned_target = target.kind == TypeKind::UInt ||
                                 target.kind == TypeKind::ULong ||
                                 target.kind == TypeKind::UChar;
    const int width =
        is_char_kind(target.kind)
            ? 8
            : (target.kind == TypeKind::Int || target.kind == TypeKind::UInt
                   ? 32
                   : 64);
    const long double modulus = std::ldexp(1.0L, width);
    if (double_source &&
        (*value < 0 ||
         *value >= (unsigned_target ? modulus
                                    : std::ldexp(1.0L, width - 1))))
      return std::nullopt;

    long double wrapped = std::fmod(std::trunc(*value), modulus);
    if (wrapped < 0) wrapped += modulus;
    const uint64_t bits = static_cast<uint64_t>(wrapped);
    if (unsigned_target)
      return std::variant<uint64_t, double>{bits};
    if (width == 32)
      return std::variant<uint64_t, double>{
          static_cast<uint64_t>(static_cast<int64_t>(
              std::bit_cast<int32_t>(static_cast<uint32_t>(bits))))};
    return std::variant<uint64_t, double>{
        static_cast<uint64_t>(std::bit_cast<int64_t>(bits))};
  }

  bool collect_static_initializers(const Initializer &initializer,
                                   std::vector<StaticInit> &values) {
    return std::visit(
        Overload{
            [&](const SingleInit &single) {
              if (initializer.type.kind == TypeKind::Pointer) {
                if (const auto *addr =
                        std::get_if<AddrOf>(&single.exp.value)) {
                  if (const auto *str =
                          std::get_if<String>(&addr->exp->value)) {
                    values.push_back(PointerInit{str->name});
                    return true;
                  }
                }
              }
              auto value = constant_initializer(single.exp, initializer.type);
              if (!value) return false;
              const Type &type = initializer.type;
              if (type.kind == TypeKind::Double) {
                const double number = std::get<double>(*value);
                if (number == 0.0 && !std::signbit(number))
                  values.push_back(ZeroInit{8});
                else
                  values.push_back(DoubleInit{number});
              } else {
                const uint64_t bits = std::get<uint64_t>(*value);
                if (bits == 0) {
                  values.push_back(ZeroInit{
                      is_char_kind(type.kind)
                          ? 1U
                          : (type.kind == TypeKind::Int ||
                                     type.kind == TypeKind::UInt
                                 ? 4U
                                 : 8U)});
                } else {
                  switch (type.kind) {
                    case TypeKind::Char:
                    case TypeKind::SChar:
                      values.push_back(
                          CharInit{static_cast<int8_t>(bits)});
                      break;
                    case TypeKind::UChar:
                      values.push_back(
                          UCharInit{static_cast<uint8_t>(bits)});
                      break;
                    case TypeKind::Int:
                      values.push_back(
                          IntInit{static_cast<int32_t>(bits)});
                      break;
                    case TypeKind::Long:
                      values.push_back(
                          LongInit{std::bit_cast<int64_t>(bits)});
                      break;
                    case TypeKind::UInt:
                      values.push_back(
                          UIntInit{static_cast<uint32_t>(bits)});
                      break;
                    case TypeKind::ULong:
                    case TypeKind::Pointer:
                      values.push_back(ULongInit{bits});
                      break;
                    default: return false;
                  }
                }
              }
              return true;
            },
            [&](const CompoundInit &compound) {
              for (const auto &child : compound.initializers) {
                if (!collect_static_initializers(child, values))
                  return false;
              }
              return true;
            }},
        initializer.value);
  }

  static Type common_type(const Type &left_in, const Type &right_in) {
    const Type left = promote_integer(left_in);
    const Type right = promote_integer(right_in);
    if (left == right) return left;
    if (left.kind == TypeKind::Double || right.kind == TypeKind::Double)
      return Type::double_type();
    const auto rank = [](TypeKind kind) {
      return kind == TypeKind::Long || kind == TypeKind::ULong ? 2 : 1;
    };
    const auto is_unsigned = [](TypeKind kind) {
      return kind == TypeKind::UInt || kind == TypeKind::ULong;
    };
    if (is_unsigned(left.kind) == is_unsigned(right.kind))
      return rank(left.kind) > rank(right.kind) ? left : right;

    const Type &unsigned_type = is_unsigned(left.kind) ? left : right;
    const Type &signed_type = is_unsigned(left.kind) ? right : left;
    if (rank(unsigned_type.kind) >= rank(signed_type.kind))
      return unsigned_type;
    if (rank(signed_type.kind) > rank(unsigned_type.kind))
      return signed_type;
    return signed_type.kind == TypeKind::Long ? Type::ulong_type()
                                               : Type::uint_type();
  }

  static bool is_arithmetic(const Type &type) {
    return type.kind != TypeKind::Pointer && type.kind != TypeKind::Function &&
           type.kind != TypeKind::Array;
  }

  static bool is_integer(const Type &type) {
    return type.kind == TypeKind::Int || type.kind == TypeKind::Long ||
           type.kind == TypeKind::UInt || type.kind == TypeKind::ULong ||
           type.kind == TypeKind::Char || type.kind == TypeKind::SChar ||
           type.kind == TypeKind::UChar;
  }

  static bool is_char_kind(TypeKind kind) {
    return kind == TypeKind::Char || kind == TypeKind::SChar ||
           kind == TypeKind::UChar;
  }

  static Type promote_integer(const Type &type) {
    return is_char_kind(type.kind) ? Type::int_type() : type;
  }

  static bool is_null_pointer_constant(const Exp &exp) {
    return std::visit(
        Overload{[](const ConstInt &c) { return c.value == 0; },
                 [](const ConstLong &c) { return c.value == 0; },
                 [](const ConstUInt &c) { return c.value == 0; },
                 [](const ConstULong &c) { return c.value == 0; },
                 [](const auto &) { return false; }},
        exp.value);
  }

  Type typecheck_and_convert(Exp &exp) {
    Type type = typecheck_exp(exp);
    if (type.kind == TypeKind::Array && type.referenced) {
      const uint32_t line = std::visit(
          Overload{[](const ConstInt &v) { return v.line; },
                        [](const ConstLong &v) { return v.line; },
                        [](const ConstUInt &v) { return v.line; },
                        [](const ConstULong &v) { return v.line; },
                        [](const ConstDouble &v) { return v.line; },
                        [](const Var &v) { return v.line; },
                        [](const Dereference &v) { return v.line; },
                        [](const AddrOf &v) { return v.line; },
                        [](const Cast &v) { return v.line; },
                        [](const Unary &v) { return v.line; },
                        [](const Binary &v) { return v.line; },
                        [](const Assignment &v) { return v.line; },
                        [](const CompoundAssignment &v) { return v.line; },
                        [](const IncDec &v) { return v.line; },
                        [](const Conditional &v) { return v.line; },
                        [](const FunctionCall &v) { return v.line; },
                        [](const Subscript &v) { return v.line; },
                 [](const String &v) { return v.line; }},
          exp.value);
      Type decayed = Type::pointer(*type.referenced);
      exp = Exp{AddrOf{std::make_unique<Exp>(std::move(exp)), line}, decayed};
      return decayed;
    }
    return type;
  }

  static bool is_lvalue(const Exp &exp) {
    return exp.type.kind != TypeKind::Function &&
           (std::holds_alternative<Var>(exp.value) ||
            std::holds_alternative<Dereference>(exp.value) ||
            std::holds_alternative<Subscript>(exp.value) ||
            std::holds_alternative<String>(exp.value));
  }

  Type common_pointer_type(Exp &left, Exp &right, uint32_t line) {
    if (left.type == right.type) return left.type;
    if (left.type.kind == TypeKind::Pointer &&
        is_null_pointer_constant(right)) {
      right = convert_to(std::move(right), left.type);
      return left.type;
    }
    if (right.type.kind == TypeKind::Pointer &&
        is_null_pointer_constant(left)) {
      left = convert_to(std::move(left), right.type);
      return right.type;
    }
    std::println("error:{}: Expressions have incompatible pointer types", line);
    had_error_ = true;
    return left.type;
  }

  void convert_by_assignment(Exp &exp, const Type &target) {
    if (exp.type == target) return;
    if (is_arithmetic(exp.type) && is_arithmetic(target)) {
      exp = convert_to(std::move(exp), target);
      return;
    }
    if (target.kind == TypeKind::Pointer && is_null_pointer_constant(exp)) {
      exp = convert_to(std::move(exp), target);
      return;
    }
    const uint32_t line = std::visit(
        Overload{[](const ConstInt &v) { return v.line; },
                 [](const ConstLong &v) { return v.line; },
                 [](const ConstUInt &v) { return v.line; },
                 [](const ConstULong &v) { return v.line; },
                 [](const ConstDouble &v) { return v.line; },
                 [](const Var &v) { return v.line; },
                 [](const Dereference &v) { return v.line; },
                 [](const AddrOf &v) { return v.line; },
                 [](const Cast &v) { return v.line; },
                 [](const Unary &v) { return v.line; },
                 [](const Binary &v) { return v.line; },
                 [](const Assignment &v) { return v.line; },
                 [](const CompoundAssignment &v) { return v.line; },
                 [](const IncDec &v) { return v.line; },
                 [](const Conditional &v) { return v.line; },
                 [](const FunctionCall &v) { return v.line; },
                 [](const Subscript &v) { return v.line; },
                 [](const String &v) { return v.line; }},
        exp.value);
    std::println("error:{}: Cannot convert type for assignment", line);
    had_error_ = true;
  }

  static Exp convert_to(Exp exp, const Type &target) {
    if (exp.type == target) return exp;
    const uint32_t line = std::visit(
        Overload{[](const ConstInt &v) { return v.line; },
                 [](const ConstLong &v) { return v.line; },
                 [](const ConstUInt &v) { return v.line; },
                 [](const ConstULong &v) { return v.line; },
                 [](const ConstDouble &v) { return v.line; },
                 [](const Var &v) { return v.line; },
                 [](const Dereference &v) { return v.line; },
                 [](const AddrOf &v) { return v.line; },
                 [](const Cast &v) { return v.line; },
                 [](const Unary &v) { return v.line; },
                 [](const Binary &v) { return v.line; },
                 [](const Assignment &v) { return v.line; },
                 [](const CompoundAssignment &v) { return v.line; },
                 [](const IncDec &v) { return v.line; },
                 [](const Conditional &v) {
                   return v.line;
                 },
                 [](const FunctionCall &v) { return v.line; },
                 [](const Subscript &v) { return v.line; },
                 [](const String &v) { return v.line; }},
        exp.value);
    Exp converted{Cast{target, std::make_unique<Exp>(std::move(exp)), line},
                  target};
    return converted;
  }

  void convert_to(std::unique_ptr<Exp> &exp, const Type &target) {
    *exp = convert_to(std::move(*exp), target);
  }

  Type typecheck_exp(Exp &exp) {
    Type result = std::visit(
        Overload{
            [](const ConstInt &) { return Type::int_type(); },
            [](const ConstLong &) { return Type::long_type(); },
            [](const ConstUInt &) { return Type::uint_type(); },
            [](const ConstULong &) { return Type::ulong_type(); },
            [](const ConstDouble &) { return Type::double_type(); },
            [&](const Var &v) {
              const Type &var_type = symbol_table().get(v.name).type;
              if (var_type.kind == TypeKind::Function) {
                std::println("error:{}: Function name used as variable",
                             v.line);
                had_error_ = true;
              }
              return var_type;
            },
            [&](const Cast &c) {
              Type source = typecheck_and_convert(*c.exp);
              if ((source.kind == TypeKind::Double &&
                   c.target_type.kind == TypeKind::Pointer) ||
                  (source.kind == TypeKind::Pointer &&
                   c.target_type.kind == TypeKind::Double) ||
                  source.kind == TypeKind::Function ||
                  c.target_type.kind == TypeKind::Function ||
                  c.target_type.kind == TypeKind::Array) {
                std::println("error:{}: Invalid cast involving pointer type",
                             c.line);
                had_error_ = true;
              }
              return c.target_type;
            },
            [&](Dereference &d) {
              Type pointer_type = typecheck_and_convert(*d.exp);
              if (pointer_type.kind != TypeKind::Pointer ||
                  !pointer_type.referenced) {
                std::println("error:{}: Cannot dereference non-pointer",
                             d.line);
                had_error_ = true;
                return Type::int_type();
              }
              return *pointer_type.referenced;
            },
            [&](AddrOf &a) {
              Type referenced = typecheck_exp(*a.exp);
              if (!is_lvalue(*a.exp)) {
                std::println("error:{}: Can't take the address of a non-lvalue",
                             a.line);
                had_error_ = true;
              }
              return Type::pointer(std::move(referenced));
            },
            [&](Subscript &s) {
              Type left = typecheck_and_convert(*s.left);
              Type right = typecheck_and_convert(*s.right);
              Type pointer_type;
              if (left.kind == TypeKind::Pointer &&
                  is_arithmetic(right) && right.kind != TypeKind::Double) {
                pointer_type = left;
              } else if (right.kind == TypeKind::Pointer &&
                         is_arithmetic(left) && left.kind != TypeKind::Double) {
                pointer_type = right;
              } else {
                std::println("error:{}: Subscript requires pointer and integer operands",
                             s.line);
                had_error_ = true;
                return Type::int_type();
              }
              return pointer_type.referenced ? *pointer_type.referenced
                                             : Type::int_type();
            },
            [&](String &s) {
              if (s.name.empty()) {
                s.name = "string." + std::to_string(next_name_id());
                Symbol::InitialValue init;
                init.kind = Symbol::InitialValue::Kind::Initial;
                for (unsigned char ch : s.value)
                  init.values.push_back(
                      CharInit{static_cast<int8_t>(ch)});
                init.values.push_back(CharInit{0});
                Type str_type =
                    Type::array(Type::char_type(), s.value.size() + 1);
                symbol_table().add(s.name, str_type,
                                   Symbol::StaticAttr{init, false});
              }
              return symbol_table().get(s.name).type;
            },
            [&](const Unary &u) {
              Type operand_type = typecheck_exp(*u.exp);
              if (std::holds_alternative<Not>(u.op))
                return Type::int_type();
              if (!is_arithmetic(operand_type)) {
                std::println(
                    "error:{}: Invalid unary operator on non-arithmetic type",
                    u.line);
                had_error_ = true;
                return operand_type;
              }
              if (std::holds_alternative<Complement>(u.op) &&
                  operand_type.kind == TypeKind::Double) {
                std::println(
                    "error:{}: Can't take the bitwise complement of a double",
                    u.line);
                had_error_ = true;
              }
              Type promoted = promote_integer(operand_type);
              if (promoted != operand_type)
                *u.exp = convert_to(std::move(*u.exp), promoted);
              return promoted;
            },
            [&](Binary &b) {
              Type left = typecheck_and_convert(*b.left);
              Type right = typecheck_and_convert(*b.right);
              if (std::holds_alternative<Remainder>(b.op) &&
                  (left.kind == TypeKind::Double ||
                   right.kind == TypeKind::Double)) {
                std::println(
                    "error:{}: Remainder operator requires integer operands",
                    b.line);
                had_error_ = true;
              }
              if (std::holds_alternative<And>(b.op) ||
                  std::holds_alternative<Or>(b.op))
                return Type::int_type();
              if (std::holds_alternative<Equal>(b.op) ||
                  std::holds_alternative<NotEqual>(b.op)) {
                if (left.kind == TypeKind::Pointer ||
                    right.kind == TypeKind::Pointer) {
                  common_pointer_type(*b.left, *b.right, b.line);
                  return Type::int_type();
                }
              }
              if (std::holds_alternative<Add>(b.op)) {
                if (left.kind == TypeKind::Pointer && is_integer(right)) {
                  convert_to(b.right, Type::long_type());
                  return left;
                }
                if (right.kind == TypeKind::Pointer && is_integer(left)) {
                  convert_to(b.left, Type::long_type());
                  return right;
                }
              }
              if (std::holds_alternative<Subtract>(b.op)) {
                if (left.kind == TypeKind::Pointer && is_integer(right)) {
                  convert_to(b.right, Type::long_type());
                  return left;
                }
                if (left.kind == TypeKind::Pointer && left == right)
                  return Type::long_type();
              }
              if ((std::holds_alternative<LessThan>(b.op) ||
                   std::holds_alternative<LessOrEqual>(b.op) ||
                   std::holds_alternative<GreaterThan>(b.op) ||
                   std::holds_alternative<GreaterOrEqual>(b.op)) &&
                  left.kind == TypeKind::Pointer && left == right)
                return Type::int_type();
              if (left.kind == TypeKind::Pointer ||
                  right.kind == TypeKind::Pointer) {
                std::println("error:{}: Unsupported operation on pointer",
                             b.line);
                had_error_ = true;
                return Type::int_type();
              }
              Type common = common_type(left, right);
              convert_to(b.left, common);
              convert_to(b.right, common);
              if (std::holds_alternative<Add>(b.op) ||
                  std::holds_alternative<Subtract>(b.op) ||
                  std::holds_alternative<Multiply>(b.op) ||
                  std::holds_alternative<Divide>(b.op) ||
                  std::holds_alternative<Remainder>(b.op) ||
                  std::holds_alternative<BitwiseAnd>(b.op) ||
                  std::holds_alternative<BitwiseOr>(b.op) ||
                  std::holds_alternative<BitwiseXor>(b.op) ||
                  std::holds_alternative<ShiftLeft>(b.op) ||
                  std::holds_alternative<ShiftRight>(b.op))
                return common;
              return Type::int_type();
            },
            [&](Assignment &a) {
              Type left = typecheck_and_convert(*a.left);
              typecheck_and_convert(*a.right);
              if (!is_lvalue(*a.left)) {
                std::println("error:{}: Expression is not a valid lvalue",
                             a.line);
                had_error_ = true;
              }
              convert_by_assignment(*a.right, left);
              return left;
            },
            [&](CompoundAssignment &a) {
              Type left = typecheck_and_convert(*a.left);
              typecheck_and_convert(*a.right);
              if (!is_lvalue(*a.left)) {
                std::println("error:{}: Expression is not a valid lvalue",
                             a.line);
                had_error_ = true;
              }
              if (left.kind == TypeKind::Pointer ||
                  a.right->type.kind == TypeKind::Pointer ||
                  left.kind == TypeKind::Array) {
                std::println("error:{}: Unsupported compound operation on pointer",
                             a.line);
                had_error_ = true;
              }
              convert_by_assignment(*a.right, left);
              return left;
            },
            [&](const IncDec &e) {
              Type type = typecheck_exp(*e.exp);
              if (!is_lvalue(*e.exp) || type.kind == TypeKind::Pointer ||
                  type.kind == TypeKind::Array ||
                  std::holds_alternative<String>(e.exp->value)) {
                std::println("error:{}: Invalid increment/decrement operand",
                             e.line);
                had_error_ = true;
              }
              return type;
            },
            [&](Conditional &c) {
              typecheck_and_convert(*c.condition);
              Type then_type = typecheck_and_convert(*c.then_exp);
              Type else_type = typecheck_and_convert(*c.else_exp);
              Type common;
              if (then_type.kind == TypeKind::Pointer ||
                  else_type.kind == TypeKind::Pointer) {
                common = common_pointer_type(*c.then_exp, *c.else_exp,
                                             c.line);
              } else {
                common = common_type(then_type, else_type);
                convert_to(c.then_exp, common);
                convert_to(c.else_exp, common);
              }
              return common;
            },
            [&](FunctionCall &c) {
              const Type &fun_type = symbol_table().get(c.name).type;
              if (fun_type.kind != TypeKind::Function) {
                std::println("error:{}: Variable used as function name",
                             c.line);
                had_error_ = true;
                for (const auto &arg : c.args) typecheck_exp(*arg);
                return Type::int_type();
              }
              if (fun_type.params.size() != c.args.size()) {
                std::println(
                    "error:{}: Function called with the wrong number of "
                    "arguments",
                    c.line);
                had_error_ = true;
              }
              for (size_t i = 0; i < c.args.size(); ++i) {
                typecheck_and_convert(*c.args[i]);
                if (i < fun_type.params.size())
                  convert_by_assignment(*c.args[i], fun_type.params[i]);
              }
              return fun_type.ret ? *fun_type.ret : Type::int_type();
            },
        },
        exp.value);
    exp.type = result;
    return result;
  }

  void typecheck_statement(Statement &stmt) {
    std::visit(
        Overload{
            [&](Return &r) {
              typecheck_and_convert(r.value);
              convert_by_assignment(r.value, return_type_);
            },
            [&](Expression &e) { typecheck_exp(e.value); },
            [](const Null &) {},
            [&](If &i) {
              typecheck_exp(i.condition);
              typecheck_statement(*i.then_stmt);
              if (i.else_stmt)
                typecheck_statement(*i.else_stmt);
            },
            [](const Goto &) {},
            [&](Label &l) { typecheck_statement(*l.stmt); },
            [&](Compound &c) { typecheck_block(*c.block); },
            [](const Break &) {},
            [](const Continue &) {},
            [&](Case &c) {
              typecheck_exp(c.value);
              typecheck_statement(*c.stmt);
            },
            [&](Default &d) { typecheck_statement(*d.stmt); },
            [&](Switch &s) {
              typecheck_exp(s.condition);
              if (!is_integer(s.condition.type)) {
                std::println(
                    "error:{}: Switch condition must have integer type",
                    s.line);
                had_error_ = true;
              } else {
                s.condition = convert_to(
                    std::move(s.condition),
                    promote_integer(s.condition.type));
              }
              typecheck_statement(*s.body);
            },
            [&](While &w) {
              typecheck_exp(w.condition->value);
              typecheck_statement(*w.body);
            },
            [&](DoWhile &d) {
              typecheck_statement(*d.body);
              typecheck_exp(d.condition->value);
            },
            [&](For &f) {
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

  void typecheck_local_variable_declaration(VariableDeclaration &d) {
    const Type &decl_type = d.var_type;
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
        } else if (!(prev->type == decl_type)) {
          std::println("error:{}: Conflicting variable types for '{}'",
                       d.line, d.name);
          had_error_ = true;
        }
      } else {
        symbol_table().add(d.name, decl_type,
                           Symbol::StaticAttr{
                               {InitialValue::Kind::NoInitializer, 0}, true});
      }
      return;
    }

    if (is_static(d)) {
      InitialValue init{InitialValue::Kind::Initial, 0};
      if (d.init) {
        typecheck_initializer(decl_type, *d.init);
        if (!collect_static_initializers(*d.init, init.values)) {
          std::println("error:{}: Non-constant initializer on local static variable '{}'",
                       d.line, d.name);
          had_error_ = true;
        }
      } else {
        Initializer zero = zero_initializer(decl_type);
        collect_static_initializers(zero, init.values);
      }
      symbol_table().add(d.name, decl_type,
                         Symbol::StaticAttr{init, false});
      return;
    }

    symbol_table().add(d.name, decl_type, Symbol::LocalAttr{});
    if (d.init)
      typecheck_initializer(decl_type, *d.init);
  }

  void typecheck_file_scope_variable_declaration(
      VariableDeclaration &d) {
    InitialValue init{InitialValue::Kind::Tentative, 0};
    if (d.init) {
      typecheck_initializer(d.var_type, *d.init);
      init.kind = InitialValue::Kind::Initial;
      if (!collect_static_initializers(*d.init, init.values)) {
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
      if (prev->type.kind == TypeKind::Function) {
        std::println("error:{}: Function redeclared as variable", d.line);
        had_error_ = true;
        return;
      }
      if (!(prev->type == d.var_type)) {
        std::println("error:{}: Conflicting variable types for '{}'",
                     d.line, d.name);
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

    symbol_table().add(d.name, d.var_type,
                       Symbol::StaticAttr{init, global});
  }

  void typecheck_function_declaration(FunctionDeclaration &d) {
    if (d.fun_type.ret && d.fun_type.ret->kind == TypeKind::Array) {
      std::println("error:{}: A function cannot return an array", d.line);
      had_error_ = true;
    }
    for (Type &param : d.fun_type.params) {
      if (param.kind == TypeKind::Array && param.referenced)
        param = Type::pointer(*param.referenced);
    }
    const Type fun_type = d.fun_type;
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
    Type previous_return_type = return_type_;
    return_type_ = d.fun_type.ret ? *d.fun_type.ret : Type::int_type();
    for (size_t i = 0; i < d.params.size(); ++i)
      symbol_table().add(d.params[i], d.fun_type.params[i],
                         Symbol::LocalAttr{});
    typecheck_block(*d.body);
    return_type_ = std::move(previous_return_type);
  }

  void typecheck_declaration(Declaration &d) {
    std::visit(
        Overload{
            [&](FunDecl &f) { typecheck_function_declaration(f.decl); },
            [&](VarDecl &v) {
            typecheck_local_variable_declaration(v.decl);
            },
        },
        d);
  }

  void typecheck_for_init(ForInit &init) {
    std::visit(
        Overload{
            [&](InitDecl &d) {
              if (d.decl.storage_class) {
                std::println("error:{}: Storage-class specifier is not allowed in a for initializer",
                             d.decl.line);
                had_error_ = true;
              }
              typecheck_local_variable_declaration(d.decl);
            },
            [&](InitExp &e) {
              if (e.exp)
                typecheck_exp(*e.exp);
            },
        },
        init);
  }

  void typecheck_block(Block &block) {
    for (auto &item : block.items) {
      std::visit(
          Overload{
              [&](Statement &stmt) { typecheck_statement(stmt); },
              [&](Declaration &d) { typecheck_declaration(d); },
          },
          item);
    }
  }

public:
  bool check(Program &program) {
    symbol_table().clear();

    for (auto &declaration : program.declarations) {
      if (auto *func = std::get_if<FunDecl>(&declaration)) {
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
                   [](ConstInt &) {},
                   [](ConstLong &) {},
                   [](ConstUInt &) {},
                   [](ConstULong &) {},
                   [](ConstDouble &) {},
                   [](Var &) {},
                   [&](Dereference &d) { find_enclosing_loop(*d.exp, loop); },
                   [&](AddrOf &a) { find_enclosing_loop(*a.exp, loop); },
                   [&](Unary &u) { find_enclosing_loop(*u.exp, loop); },
                   [&](Cast &c) { find_enclosing_loop(*c.exp, loop); },
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
                   [&](Subscript &s) {
                     find_enclosing_loop(*s.left, loop);
                     find_enclosing_loop(*s.right, loop);
                   },
                   [](String &) {},
               },
               exp.value);
  }

  void find_enclosing_loop(Initializer &init, const Targets *loop) {
    std::visit(
        Overload{
            [&](SingleInit &single) {
              find_enclosing_loop(single.exp, loop);
            },
            [&](CompoundInit &compound) {
              for (auto &child : compound.initializers)
                find_enclosing_loop(child, loop);
            }},
        init.value);
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
    int32_t value = 0;
    if (const auto *constant = std::get_if<ConstInt>(&c.value.value)) {
      value = constant->value;
    } else if (const auto *constant =
                   std::get_if<ConstLong>(&c.value.value)) {
      value = static_cast<int32_t>(constant->value);
    } else if (const auto *constant =
                   std::get_if<ConstUInt>(&c.value.value)) {
      value = static_cast<int32_t>(constant->value);
    } else if (const auto *constant =
                   std::get_if<ConstULong>(&c.value.value)) {
      value = static_cast<int32_t>(constant->value);
    } else {
      std::println("error:{}: Case value must be a constant expression",
                   c.line);
      had_error_ = true;
      return;
    }
    if (!cases.values.insert(value).second) {
      std::println("error:{}: Duplicate case value '{}'", c.line, value);
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