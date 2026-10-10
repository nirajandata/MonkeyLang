module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

export module ast;

export {
  template <typename... Ts> struct Overload : Ts... {
    using Ts::operator()...;
  };

  enum class TypeKind {
    Int,
    Long,
    UInt,
    ULong,
    Double,
    Char,
    SChar,
    UChar,
    Void,
    Function,
    Pointer,
    Array,
    Structure
  };

  struct StructDef;

  struct Type {
    TypeKind kind = TypeKind::Int;
    std::vector<Type> params;
    std::shared_ptr<Type> ret;
    std::shared_ptr<Type> referenced;
    size_t size = 0;
    std::shared_ptr<StructDef> struct_def;
    bool incomplete_array_element = false;
    bool incomplete_at_declaration = false;

    static Type int_type() { return {}; }
    static Type long_type() { return {TypeKind::Long, {}, nullptr, nullptr, 0}; }
    static Type uint_type() { return {TypeKind::UInt, {}, nullptr, nullptr, 0}; }
    static Type ulong_type() { return {TypeKind::ULong, {}, nullptr, nullptr, 0}; }
    static Type double_type() { return {TypeKind::Double, {}, nullptr, nullptr, 0}; }
    static Type char_type() { return {TypeKind::Char, {}, nullptr, nullptr, 0}; }
    static Type schar_type() { return {TypeKind::SChar, {}, nullptr, nullptr, 0}; }
    static Type uchar_type() { return {TypeKind::UChar, {}, nullptr, nullptr, 0}; }
    static Type void_type() { return {TypeKind::Void, {}, nullptr, nullptr, 0}; }
    static Type function(std::vector<Type> params, Type ret) {
      return {TypeKind::Function, std::move(params),
              std::make_shared<Type>(std::move(ret)), nullptr, 0};
    }
    static Type pointer(Type referenced) {
      return {TypeKind::Pointer, {},
              nullptr, std::make_shared<Type>(std::move(referenced)), 0};
    }
    static Type array(Type element, size_t size) {
      return {TypeKind::Array, {}, nullptr,
              std::make_shared<Type>(std::move(element)), size};
    }
    static Type structure(std::shared_ptr<StructDef> def) {
      Type type;
      type.kind = TypeKind::Structure;
      type.struct_def = std::move(def);
      return type;
    }

    friend bool operator==(const Type &a, const Type &b) {
      if (a.kind != b.kind || a.params != b.params || a.size != b.size ||
          a.incomplete_array_element != b.incomplete_array_element ||
          static_cast<bool>(a.ret) != static_cast<bool>(b.ret) ||
          static_cast<bool>(a.referenced) != static_cast<bool>(b.referenced) ||
          static_cast<bool>(a.struct_def) !=
              static_cast<bool>(b.struct_def))
        return false;
      if (a.kind == TypeKind::Structure)
        return a.struct_def.get() == b.struct_def.get();
      return (!a.ret || *a.ret == *b.ret) &&
             (!a.referenced || *a.referenced == *b.referenced);
    }
  };

  struct StructMember {
    std::string name;
    Type type;
    size_t offset = 0;
    uint32_t line = 0;
  };

  struct StructDef {
    std::string tag;
    bool declared = false;
    bool complete = false;
    bool redefinition = false;
  bool invalid = false;
  bool is_union = false;
    std::vector<StructMember> members;
    size_t size = 0;
    size_t alignment = 1;
  };

  struct ConstInt {
    int32_t value;
    uint32_t line;
  };

  struct ConstLong {
    int64_t value;
    uint32_t line;
  };

  struct ConstUInt {
    uint32_t value;
    uint32_t line;
  };

  struct ConstULong {
    uint64_t value;
    uint32_t line;
  };

  struct ConstDouble {
    double value;
    uint32_t line;
  };

  struct IntInit {
    int32_t value;
  };

  struct LongInit {
    int64_t value;
  };

  struct UIntInit {
    uint32_t value;
  };

  struct ULongInit {
    uint64_t value;
  };

  struct DoubleInit {
    double value;
  };

  struct ZeroInit {
    size_t bytes;
  };

  struct CharInit {
    int8_t value;
  };

  struct UCharInit {
    uint8_t value;
  };

  struct PointerInit {
    std::string name;
  };

  using StaticInit =
      std::variant<IntInit, LongInit, UIntInit, ULongInit, DoubleInit,
                   ZeroInit, CharInit, UCharInit, PointerInit>;

  struct Complement {};

  struct Negate {};

  struct Not {};

  using UnaryOp = std::variant<Complement, Negate, Not>;

  struct Static {};

  struct Extern {};

  using StorageClass = std::variant<Static, Extern>;

  struct Add {};

  struct Subtract {};

  struct Multiply {};

  struct Divide {};

  struct Remainder {};

  struct And {};

  struct Or {};

  struct Equal {};

  struct NotEqual {};

  struct LessThan {};

  struct LessOrEqual {};

  struct GreaterThan {};

  struct GreaterOrEqual {};

  struct BitwiseAnd {};

  struct BitwiseOr {};

  struct BitwiseXor {};

  struct ShiftLeft {};

  struct ShiftRight {};

  using BinaryOp =
      std::variant<Add, Subtract, Multiply, Divide, Remainder, And, Or, Equal,
                   NotEqual, LessThan, LessOrEqual, GreaterThan, GreaterOrEqual,
                   BitwiseAnd, BitwiseOr, BitwiseXor, ShiftLeft, ShiftRight>;

  using CompoundOp =
      std::variant<Add, Subtract, Multiply, Divide, Remainder, BitwiseAnd,
                   BitwiseOr, BitwiseXor, ShiftLeft, ShiftRight>;

  struct Increment {};

  struct Decrement {};

  using IncDecOp = std::variant<Increment, Decrement>;

  struct Exp;

  struct Var {
    std::string name;
    uint32_t line;
  };

  struct Cast {
    Type target_type;
    std::unique_ptr<Exp> exp;
    uint32_t line;
  };

  struct Unary {
    UnaryOp op;
    std::unique_ptr<Exp> exp;
    uint32_t line;
  };

  struct Binary {
    BinaryOp op;
    std::unique_ptr<Exp> left;
    std::unique_ptr<Exp> right;
    uint32_t line;
  };

  struct Assignment {
    std::unique_ptr<Exp> left;
    std::unique_ptr<Exp> right;
    uint32_t line;
  };

  struct CompoundAssignment {
    CompoundOp op;
    std::unique_ptr<Exp> left;
    std::unique_ptr<Exp> right;
    uint32_t line;
  };

  struct IncDec {
    IncDecOp op;
    std::unique_ptr<Exp> exp;
    bool postfix;
    uint32_t line;
  };

  struct Conditional {
    std::unique_ptr<Exp> condition;
    std::unique_ptr<Exp> then_exp;
    std::unique_ptr<Exp> else_exp;
    uint32_t line;
  };

  struct FunctionCall {
    std::string name;
    std::vector<std::unique_ptr<Exp>> args;
    uint32_t line;
  };

  struct Dereference {
    std::unique_ptr<Exp> exp;
    uint32_t line;
  };

  struct AddrOf {
    std::unique_ptr<Exp> exp;
    uint32_t line;
  };

  struct Subscript {
    std::unique_ptr<Exp> left;
    std::unique_ptr<Exp> right;
    uint32_t line;
  };

  struct String {
    std::string value;
    std::string name;
    uint32_t line;
  };

  struct SizeOfT {
    Type target_type;
    uint32_t line;
  };

  struct SizeOf {
    std::unique_ptr<Exp> exp;
    uint32_t line;
  };

  struct MemberAccess {
    std::unique_ptr<Exp> exp;
    std::string member;
    bool through_pointer = false;
    size_t offset = 0;
    uint32_t line;
  };

  struct Exp {
    std::variant<ConstInt, ConstLong, ConstUInt, ConstULong, ConstDouble, Var,
                 Cast, Unary, Binary, Assignment, CompoundAssignment, IncDec,
                 Conditional, FunctionCall, Dereference, AddrOf, Subscript,
                 String, SizeOfT, SizeOf, MemberAccess> value;
    Type type;
  };

  struct Initializer;
  struct SingleInit {
    Exp exp;
  };
  struct CompoundInit {
    std::vector<Initializer> initializers;
  };
  struct Initializer {
    std::variant<SingleInit, CompoundInit> value;
    Type type;
  };

  struct Return {
    std::optional<Exp> value;
    uint32_t line;
  };

  struct Expression {
    Exp value;
    uint32_t line;
  };

  struct Null {};

  struct Statement;

  struct If {
    Exp condition;
    std::unique_ptr<Statement> then_stmt;
    std::unique_ptr<Statement> else_stmt;
    uint32_t line;
  };

  struct Goto {
    std::string label;
    uint32_t line;
  };

  struct Label {
    std::string name;
    std::unique_ptr<Statement> stmt;
    uint32_t line;
  };

  struct Block;

  struct Compound {
    std::unique_ptr<Block> block;
    uint32_t line;
  };

  struct Break {
    std::string label;
    uint32_t line;
  };

  struct Continue {
    std::string label;
    uint32_t line;
  };

  struct While {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> body;
    std::string break_label;
    std::string continue_label;
    uint32_t line;
  };

  struct DoWhile {
    std::unique_ptr<Statement> body;
    std::unique_ptr<Expression> condition;
    std::string break_label;
    std::string continue_label;
    uint32_t line;
  };

  struct VariableDeclaration {
    std::string name;
    std::optional<Initializer> init;
    uint32_t line;
    std::optional<StorageClass> storage_class;
    Type var_type;
  };

  struct FunctionDeclaration {
    std::string name;
    std::vector<std::string> params;
    std::unique_ptr<Block> body;
    uint32_t line;
    std::optional<StorageClass> storage_class;
    Type fun_type;
  };

  struct FunDecl {
    FunctionDeclaration decl;
  };

  struct VarDecl {
    VariableDeclaration decl;
  };

  struct StructDecl {
    std::shared_ptr<StructDef> def;
    uint32_t line;
  };

  using Declaration = std::variant<FunDecl, VarDecl, StructDecl>;

  struct InitDecl {
    VariableDeclaration decl;
  };

  struct InitExp {
    std::optional<Exp> exp;
  };

  using ForInit = std::variant<InitDecl, InitExp>;

  struct For {
    ForInit init;
    std::optional<Expression> condition;
    std::optional<Expression> post;
    std::unique_ptr<Statement> body;
    std::string break_label;
    std::string continue_label;
    uint32_t line;
  };

  struct Case {
    Exp value;
    std::string label;
    std::unique_ptr<Statement> stmt;
    uint32_t line;
  };

  struct Default {
    std::string label;
    std::unique_ptr<Statement> stmt;
    uint32_t line;
  };

  struct Switch {
    Exp condition;
    std::unique_ptr<Statement> body;
    std::string break_label;
    std::vector<Case *> cases;
    Default *default_case;
    uint32_t line;
  };

  struct Statement {
    std::variant<Return, Expression, Null, If, Goto, Label, Compound, Break,
                 Continue, Case, Default, Switch, While, DoWhile, For> value;
  };

  using BlockItem = std::variant<Statement, Declaration>;

  struct Block {
    std::vector<BlockItem> items;
  };

  struct Program {
    std::vector<Declaration> declarations;
  };
}
