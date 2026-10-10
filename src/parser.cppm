module;

#include <charconv>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <meta>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <utility>
#include <vector>

export module parser;

import token;
import ast;

template <typename AST_Node>
consteval std::meta::info get_token_type_info() {
  std::string_view ast_name = std::meta::identifier_of(^^AST_Node);
  for (std::meta::info e : std::meta::enumerators_of(^^TokenType)) {
    if (std::meta::identifier_of(e) == ast_name) return e;
  }
  throw "No matching token type!";
}

template <typename AST_Node>
consteval TokenType get_token_type() {
  return [: get_token_type_info<AST_Node>() :];
}

template <typename Variant, std::size_t... Is>
Variant match_token_to_variant_impl(TokenType type, std::index_sequence<Is...>) {
  std::optional<Variant> result;

  if (!((type == get_token_type<std::variant_alternative_t<Is, Variant>>() &&
        (result = std::variant_alternative_t<Is, Variant>{}, true)) || ...)) {
    std::unreachable();
  }

  return *std::move(result);
}

template <typename Variant>
Variant match_token_to_variant(TokenType type) {
  return match_token_to_variant_impl<Variant>(type, std::make_index_sequence<std::variant_size_v<Variant>>{});
}

static std::optional<CompoundOp> compound_op(TokenType type) {
  switch (type) {
    case TokenType::AddAssign: return CompoundOp{Add{}};
    case TokenType::SubtractAssign: return CompoundOp{Subtract{}};
    case TokenType::MultiplyAssign: return CompoundOp{Multiply{}};
    case TokenType::DivideAssign: return CompoundOp{Divide{}};
    case TokenType::RemainderAssign: return CompoundOp{Remainder{}};
    case TokenType::BitwiseAndAssign: return CompoundOp{BitwiseAnd{}};
    case TokenType::BitwiseOrAssign: return CompoundOp{BitwiseOr{}};
    case TokenType::BitwiseXorAssign: return CompoundOp{BitwiseXor{}};
    case TokenType::ShiftLeftAssign: return CompoundOp{ShiftLeft{}};
    case TokenType::ShiftRightAssign: return CompoundOp{ShiftRight{}};
    default: return std::nullopt;
  }
}

static IncDecOp inc_dec_op(TokenType type) {
  return type == TokenType::Increment ? IncDecOp{Increment{}} : IncDecOp{Decrement{}};
}

export class Parser {
  struct Declarator;
  struct ParameterSyntax {
    Type base_type;
    std::unique_ptr<Declarator> declarator;
  };
  struct Declarator {
    enum class Kind { Identifier, Pointer, Function, Array } kind;
    std::string name;
    std::unique_ptr<Declarator> inner;
    std::vector<ParameterSyntax> params;
    size_t array_size = 0;
  };
  struct ProcessedDeclarator {
    std::string name;
    Type type;
    std::vector<std::string> param_names;
  };

  const std::vector<Token>& tokens_;
  size_t pos_ = 0;
  bool had_error_ = false;
  std::string_view filename_;

  const Token& peek(size_t offset = 0) const { return tokens_[pos_ + offset]; }

  Token advance() { return tokens_[pos_++]; }

  bool check(TokenType type, size_t offset = 0) const {
    return peek(offset).type == type;
  }

  bool match(TokenType type) {
    if (check(type)) {
      advance();
      return true;
    }
    return false;
  }

  void expect(TokenType type, std::string_view expected) {
    if (!match(type)) {
      std::println("error:{}: Expected {} but found '{}'", peek().line, expected, peek().text);
      had_error_ = true;
    }
  }

  static int decode_escape(std::string_view text, size_t pos,
                           size_t &consumed) {
    const char c = text[pos];
    switch (c) {
    case '\'':
      consumed = 1;
      return '\'';
    case '"':
      consumed = 1;
      return '"';
    case '?':
      consumed = 1;
      return '?';
    case '\\':
      consumed = 1;
      return '\\';
    case 'a':
      consumed = 1;
      return 7;
    case 'b':
      consumed = 1;
      return 8;
    case 'f':
      consumed = 1;
      return 12;
    case 'n':
      consumed = 1;
      return 10;
    case 'r':
      consumed = 1;
      return 13;
    case 't':
      consumed = 1;
      return 9;
    case 'v':
      consumed = 1;
      return 11;
    default: {
      int value = 0;
      size_t n = 0;
      while (n < 3 && pos + n < text.size() && text[pos + n] >= '0' &&
             text[pos + n] <= '7') {
        value = value * 8 + (text[pos + n] - '0');
        ++n;
      }
      consumed = n;
      return value;
    }
    }
  }

  static Exp decode_char_constant(std::string_view text, uint32_t line) {
    int value = 0;
    const size_t end = text.size() > 1 ? text.size() - 1 : 1;
    size_t i = 1;
    while (i < end) {
      if (text[i] == '\\') {
        size_t consumed = 0;
        const int v = decode_escape(text, i + 1, consumed);
        value = (value << 8) | (v & 0xff);
        i += 1 + consumed;
      } else {
        value = (value << 8) |
                (static_cast<int>(static_cast<unsigned char>(text[i])) & 0xff);
        ++i;
      }
    }
    return Exp{ConstInt{static_cast<int32_t>(value), line}};
  }

  static std::string decode_string_literal(std::string_view text) {
    std::string result;
    const size_t end = text.size() > 1 ? text.size() - 1 : 1;
    size_t i = 1;
    while (i < end) {
      if (text[i] == '\\') {
        size_t consumed = 0;
        const int v = decode_escape(text, i + 1, consumed);
        result.push_back(static_cast<char>(static_cast<unsigned char>(v)));
        i += 1 + consumed;
      } else {
        result.push_back(text[i]);
        ++i;
      }
    }
    return result;
  }

  Exp parse_constant() {
    const Token& tok = advance();
    if (tok.type == TokenType::FloatingConstant) {
      std::string text(tok.text);
      char *end = nullptr;
      const double value = std::strtod(text.c_str(), &end);
      if (end != text.c_str() + text.size()) {
        std::println("error:{}: Invalid floating-point constant '{}'",
                     tok.line, tok.text);
        had_error_ = true;
        return Exp{ConstDouble{0.0, tok.line}};
      }
      return Exp{ConstDouble{value, tok.line}};
    }
    if (tok.type == TokenType::CharConstant) {
      return decode_char_constant(tok.text, tok.line);
    }
    if (tok.type != TokenType::Constant &&
        tok.type != TokenType::LongConstant &&
        tok.type != TokenType::UnsignedConstant &&
        tok.type != TokenType::UnsignedLongConstant) {
      std::println("error:{}: Expected constant but found '{}'", tok.line, tok.text);
      had_error_ = true;
      return Exp{ConstInt{0, tok.line}};
    }

    const bool unsigned_constant =
        tok.type == TokenType::UnsignedConstant ||
        tok.type == TokenType::UnsignedLongConstant;
    const bool ulong_constant = tok.type == TokenType::UnsignedLongConstant;
    const size_t suffix_length =
        tok.type == TokenType::LongConstant ? 1 :
        tok.type == TokenType::UnsignedConstant ? 1 :
        tok.type == TokenType::UnsignedLongConstant ? 2 : 0;
    const char *end = tok.text.data() + tok.text.size() - suffix_length;
    uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(tok.text.data(), end, value);
    const bool out_of_range =
        ec != std::errc{} || ptr != end ||
        (!unsigned_constant &&
         value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
    if (out_of_range) {
      std::println("error:{}: Constant '{}' is too large to represent",
                   tok.line, tok.text);
      had_error_ = true;
      return Exp{ConstInt{0, tok.line}};
    }
    if (ulong_constant)
      return Exp{ConstULong{value, tok.line}};
    if (unsigned_constant) {
      if (value <= std::numeric_limits<uint32_t>::max())
        return Exp{ConstUInt{static_cast<uint32_t>(value), tok.line}};
      return Exp{ConstULong{value, tok.line}};
    }
    const bool long_constant = tok.type == TokenType::LongConstant;
    if (!long_constant &&
        value <= static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
      return Exp{ConstInt{static_cast<int32_t>(value), tok.line}};
    return Exp{ConstLong{static_cast<int64_t>(value), tok.line}};
  }

  std::string parse_identifier() {
    const Token& name_tok = advance();
    if (name_tok.type != TokenType::Identifier) {
      std::println("error:{}: Expected identifier but found '{}'", name_tok.line, name_tok.text);
      had_error_ = true;
    }
    return std::string(name_tok.text);
  }

  FunctionCall parse_function_call(const Token &name_tok) {
    expect(TokenType::LParen, "\"(\"");

    std::vector<std::unique_ptr<Exp>> args;
    if (!check(TokenType::RParen)) {
      do {
        args.push_back(std::make_unique<Exp>(parse_exp(0)));
      } while (match(TokenType::Comma));
    }
    expect(TokenType::RParen, "\")\"");

    return {std::string(name_tok.text), std::move(args), name_tok.line};
  }

  Initializer parse_initializer() {
    if (!match(TokenType::LBrace))
      return Initializer{SingleInit{parse_exp(0)}, {}};

    std::vector<Initializer> initializers;
    if (!check(TokenType::RBrace)) {
      do {
        initializers.push_back(parse_initializer());
      } while (match(TokenType::Comma) && !check(TokenType::RBrace));
    }
    expect(TokenType::RBrace, "\"}\"");
    if (initializers.empty()) {
      std::println("error:{}: Empty initializer list", peek().line);
      had_error_ = true;
    }
    return Initializer{CompoundInit{std::move(initializers)}, {}};
  }

  Exp parse_primary() {
    auto tok = peek();

    switch (tok.type) {
      case TokenType::Constant:
      case TokenType::LongConstant:
      case TokenType::UnsignedConstant:
      case TokenType::UnsignedLongConstant:
      case TokenType::FloatingConstant:
        return parse_constant();

      case TokenType::CharConstant: {
        advance();
        return decode_char_constant(tok.text, tok.line);
      }

      case TokenType::StringLiteral: {
        advance();
        std::string value = decode_string_literal(tok.text);
        while (check(TokenType::StringLiteral))
          value += decode_string_literal(advance().text);
        return Exp{String{std::move(value), {}, tok.line}};
      }

      case TokenType::Identifier: {
        advance();
        if (check(TokenType::LParen)) {
          return Exp{parse_function_call(tok)};
        }
        return Exp{Var{std::string(tok.text), tok.line}};
      }

      case TokenType::Increment:
      case TokenType::Decrement: {
        advance();
        return Exp{IncDec{inc_dec_op(tok.type),
                           std::make_unique<Exp>(parse_postfix()), false,
                           tok.line}};
      }

      case TokenType::Complement:
      case TokenType::Subtract:
      case TokenType::Not: {
        advance();
        UnaryOp op = (tok.type == TokenType::Complement) ? UnaryOp{Complement{}}
                   : (tok.type == TokenType::Subtract) ? UnaryOp{Negate{}}
                   : UnaryOp{Not{}};
        return Exp{Unary{std::move(op), std::make_unique<Exp>(parse_postfix()), tok.line}};
      }

      case TokenType::Multiply:
      case TokenType::BitwiseAnd: {
        advance();
        Exp inner = parse_postfix();
        if (tok.type == TokenType::Multiply)
          return Exp{Dereference{std::make_unique<Exp>(std::move(inner)),
                                 tok.line}};
        return Exp{AddrOf{std::make_unique<Exp>(std::move(inner)), tok.line}};
      }

      case TokenType::Sizeof: {
        advance();
        if (check(TokenType::LParen) &&
            (check(TokenType::Int, 1) || check(TokenType::Long, 1) ||
             check(TokenType::Signed, 1) || check(TokenType::Unsigned, 1) ||
             check(TokenType::Double, 1) || check(TokenType::Char, 1) ||
             check(TokenType::Void, 1))) {
          advance();
          Type target_type = parse_type_specifiers();
          if (check(TokenType::Multiply) || check(TokenType::LParen) ||
              check(TokenType::LBracket))
            target_type = process_abstract_declarator(
                parse_abstract_declarator(), std::move(target_type));
          expect(TokenType::RParen, "\")\"");
          return Exp{SizeOfT{std::move(target_type), tok.line}};
        }
        Exp operand = parse_postfix();
        return Exp{SizeOf{std::make_unique<Exp>(std::move(operand)), tok.line}};
      }

      case TokenType::LParen: {
        advance();
        if (check(TokenType::Int) || check(TokenType::Long) ||
            check(TokenType::Signed) || check(TokenType::Unsigned) ||
            check(TokenType::Double) || check(TokenType::Char) ||
            check(TokenType::Void)) {
          Type target_type = parse_type_specifiers();
          if (check(TokenType::Multiply) || check(TokenType::LParen) ||
              check(TokenType::LBracket))
            target_type = process_abstract_declarator(
                parse_abstract_declarator(), std::move(target_type));
          expect(TokenType::RParen, "\")\"");
          return Exp{Cast{std::move(target_type),
                          std::make_unique<Exp>(parse_postfix()), tok.line}};
        }
        auto inner = parse_exp(0);
        expect(TokenType::RParen, "\")\"");
        return inner;
      }

      default:
        std::println("error:{}: Expected expression but found '{}'", tok.line, tok.text);
        had_error_ = true;
        advance();
        return Exp{ConstInt{0, tok.line}};
    }
  }

  Exp parse_postfix() {
    auto exp = parse_primary();
    while (check(TokenType::Increment) || check(TokenType::Decrement) ||
           check(TokenType::LBracket)) {
      if (match(TokenType::LBracket)) {
        const uint32_t line = peek().line;
        auto index = parse_exp(0);
        expect(TokenType::RBracket, "\"]\"");
        exp = Exp{Subscript{std::make_unique<Exp>(std::move(exp)),
                           std::make_unique<Exp>(std::move(index)), line}};
        continue;
      }
      auto tok = advance();
      exp = Exp{IncDec{inc_dec_op(tok.type),
                        std::make_unique<Exp>(std::move(exp)), true, tok.line}};
    }
    return exp;
  }

  static int binary_prec(TokenType type) {
    switch (type) {
      case TokenType::Assign:
      case TokenType::AddAssign:
      case TokenType::SubtractAssign:
      case TokenType::MultiplyAssign:
      case TokenType::DivideAssign:
      case TokenType::RemainderAssign:
      case TokenType::BitwiseAndAssign:
      case TokenType::BitwiseOrAssign:
      case TokenType::BitwiseXorAssign:
      case TokenType::ShiftLeftAssign:
      case TokenType::ShiftRightAssign: return 1;
      case TokenType::QuestionMark: return 2;
      case TokenType::Or: return 5;
      case TokenType::And: return 10;
      case TokenType::BitwiseOr: return 15;
      case TokenType::BitwiseXor: return 20;
      case TokenType::BitwiseAnd: return 25;
      case TokenType::Equal:
      case TokenType::NotEqual: return 30;
      case TokenType::LessThan:
      case TokenType::LessOrEqual:
      case TokenType::GreaterThan:
      case TokenType::GreaterOrEqual: return 35;
      case TokenType::ShiftLeft:
      case TokenType::ShiftRight: return 40;
      case TokenType::Add:
      case TokenType::Subtract: return 45;
      case TokenType::Multiply:
      case TokenType::Divide:
      case TokenType::Remainder: return 50;
      default: return 0;
    }
  }

  Exp parse_exp(int min_prec) {
    auto left = parse_postfix();

    while (true) {
      int prec = binary_prec(peek().type);
      if (prec == 0 || prec < min_prec) break;

      auto tok = advance();
      if (tok.type == TokenType::Assign) {
        auto right = parse_exp(prec);
        left = Exp{Assignment{
            std::make_unique<Exp>(std::move(left)),
            std::make_unique<Exp>(std::move(right)),
            tok.line}};
      } else if (auto op = compound_op(tok.type)) {
        auto right = parse_exp(prec);
        left = Exp{CompoundAssignment{
            std::move(*op),
            std::make_unique<Exp>(std::move(left)),
            std::make_unique<Exp>(std::move(right)),
            tok.line}};
      } else if (tok.type == TokenType::QuestionMark) {
        auto middle = parse_exp(0);
        expect(TokenType::Colon, "\":\"");
        auto right = parse_exp(prec);
        left = Exp{Conditional{
            std::make_unique<Exp>(std::move(left)),
            std::make_unique<Exp>(std::move(middle)),
            std::make_unique<Exp>(std::move(right)),
            tok.line}};
      } else {
        left = Exp{Binary{
            match_token_to_variant<BinaryOp>(tok.type),
            std::make_unique<Exp>(std::move(left)),
            std::make_unique<Exp>(parse_exp(prec + 1)),
            tok.line}};
      }
    }

    return left;
  }

  Return parse_return() {
    uint32_t line = peek().line;
    expect(TokenType::Return, "\"return\"");
    std::optional<Exp> val;
    if (!check(TokenType::Semicolon))
      val = parse_exp(0);
    expect(TokenType::Semicolon, "\";\"");
    return {std::move(val), line};
  }

  std::optional<Exp> parse_optional_exp(TokenType end) {
    if (check(end) || check(TokenType::Eof)) return std::nullopt;
    return parse_exp(0);
  }

  static std::optional<Expression> as_statement_exp(std::optional<Exp> exp, uint32_t line) {
    if (!exp) return std::nullopt;
    return Expression{std::move(*exp), line};
  }

  Statement parse_break() {
    uint32_t line = peek().line;
    expect(TokenType::Break, "\"break\"");
    expect(TokenType::Semicolon, "\";\"");
    return Statement{Break{{}, line}};
  }

  Statement parse_continue() {
    uint32_t line = peek().line;
    expect(TokenType::Continue, "\"continue\"");
    expect(TokenType::Semicolon, "\";\"");
    return Statement{Continue{{}, line}};
  }

  Statement parse_while() {
    uint32_t line = peek().line;
    expect(TokenType::While, "\"while\"");
    expect(TokenType::LParen, "\"(\"");
    uint32_t condition_line = peek().line;
    auto condition = std::make_unique<Expression>(parse_exp(0), condition_line);
    expect(TokenType::RParen, "\")\"");
    auto body = std::make_unique<Statement>(parse_statement());
    return Statement{While{std::move(condition), std::move(body), {}, {}, line}};
  }

  Statement parse_do_while() {
    uint32_t line = peek().line;
    expect(TokenType::Do, "\"do\"");
    auto body = std::make_unique<Statement>(parse_statement());
    expect(TokenType::While, "\"while\"");
    expect(TokenType::LParen, "\"(\"");
    uint32_t condition_line = peek().line;
    auto condition = std::make_unique<Expression>(parse_exp(0), condition_line);
    expect(TokenType::RParen, "\")\"");
    expect(TokenType::Semicolon, "\";\"");
    return Statement{DoWhile{std::move(body), std::move(condition), {}, {}, line}};
  }

  ForInit parse_for_init() {
    if (is_declaration_start()) {
      uint32_t line = peek().line;
      auto decl = parse_declaration();
      if (auto *variable = std::get_if<VarDecl>(&decl))
        return ForInit{InitDecl{std::move(variable->decl)}};

      std::println("error:{}: Function declaration is not allowed in a for initializer",
                   line);
      had_error_ = true;
      return ForInit{InitExp{std::nullopt}};
    }
    auto exp = parse_optional_exp(TokenType::Semicolon);
    expect(TokenType::Semicolon, "\";\"");
    return ForInit{InitExp{std::move(exp)}};
  }

  Statement parse_for() {
    uint32_t line = peek().line;
    expect(TokenType::For, "\"for\"");
    expect(TokenType::LParen, "\"(\"");

    auto init = parse_for_init();

    uint32_t condition_line = peek().line;
    auto condition = as_statement_exp(parse_optional_exp(TokenType::Semicolon), condition_line);
    expect(TokenType::Semicolon, "\";\"");

    uint32_t post_line = peek().line;
    auto post = as_statement_exp(parse_optional_exp(TokenType::RParen), post_line);
    expect(TokenType::RParen, "\")\"");

    auto body = std::make_unique<Statement>(parse_statement());
    return Statement{For{std::move(init), std::move(condition),
                         std::move(post), std::move(body), {}, {}, line}};
  }

  Statement parse_case() {
    uint32_t line = peek().line;
    expect(TokenType::Case, "\"case\"");
    auto value = parse_exp(0);
    expect(TokenType::Colon, "\":\"");
    auto stmt = std::make_unique<Statement>(parse_statement());
    return Statement{Case{std::move(value), {}, std::move(stmt), line}};
  }

  Statement parse_default() {
    uint32_t line = peek().line;
    expect(TokenType::Default, "\"default\"");
    expect(TokenType::Colon, "\":\"");
    auto stmt = std::make_unique<Statement>(parse_statement());
    return Statement{Default{{}, std::move(stmt), line}};
  }

  Statement parse_switch() {
    uint32_t line = peek().line;
    expect(TokenType::Switch, "\"switch\"");
    expect(TokenType::LParen, "\"(\"");
    auto condition = parse_exp(0);
    expect(TokenType::RParen, "\")\"");
    auto body = std::make_unique<Statement>(parse_statement());
    return Statement{Switch{std::move(condition), std::move(body), {}, {}, nullptr, line}};
  }

  Statement parse_if() {
    uint32_t line = peek().line;
    expect(TokenType::If, "\"if\"");
    expect(TokenType::LParen, "\"(\"");
    auto condition = parse_exp(0);
    expect(TokenType::RParen, "\")\"");

    auto then_stmt = std::make_unique<Statement>(parse_statement());

    std::unique_ptr<Statement> else_stmt;
    if (match(TokenType::Else)) {
      else_stmt = std::make_unique<Statement>(parse_statement());
    }

    return Statement{If{std::move(condition), std::move(then_stmt), std::move(else_stmt), line}};
  }

  Statement parse_goto() {
    uint32_t line = peek().line;
    expect(TokenType::Goto, "\"goto\"");
    auto name = parse_identifier();
    expect(TokenType::Semicolon, "\";\"");
    return Statement{Goto{std::move(name), line}};
  }

  Statement parse_label() {
    uint32_t line = peek().line;
    auto name = parse_identifier();
    expect(TokenType::Colon, "\":\"");
    auto stmt = std::make_unique<Statement>(parse_statement());
    return Statement{Label{std::move(name), std::move(stmt), line}};
  }

  Statement parse_statement() {
    switch (peek().type) {
      case TokenType::Return:   return Statement{parse_return()};
      case TokenType::If:       return parse_if();
      case TokenType::Break:    return parse_break();
      case TokenType::Continue: return parse_continue();
      case TokenType::Case:     return parse_case();
      case TokenType::Default:  return parse_default();
      case TokenType::Switch:   return parse_switch();
      case TokenType::While:    return parse_while();
      case TokenType::Do:       return parse_do_while();
      case TokenType::For:      return parse_for();
      case TokenType::Goto:     return parse_goto();
      case TokenType::LBrace:   return parse_compound_stmt();
      case TokenType::Semicolon:
        advance();
        return Statement{Null{}};

      case TokenType::Identifier:
        if (check(TokenType::Colon, 1)) {
          return parse_label();
        }
        [[fallthrough]];
      default: {
        uint32_t line = peek().line;
        auto exp = parse_exp(0);
        expect(TokenType::Semicolon, "\";\"");
        return Statement{Expression{std::move(exp), line}};
      }
    }
  }

  Type type_from_specifiers(const std::vector<TokenType> &types,
                            uint32_t line) {
    const auto count_of = [&](TokenType t) {
      return std::count(types.begin(), types.end(), t);
    };
    if (count_of(TokenType::Void) != 0) {
      if (types.size() == 1)
        return Type::void_type();
      std::println("error:{}: Can't combine 'void' with other type specifiers",
                   line);
      had_error_ = true;
      return Type::void_type();
    }
    if (count_of(TokenType::Double) != 0) {
      if (types.size() == 1)
        return Type::double_type();
      std::println("error:{}: Can't combine 'double' with other type specifiers",
                   line);
      had_error_ = true;
      return Type::double_type();
    }
    if (count_of(TokenType::Char) != 0) {
      if (count_of(TokenType::Char) > 1 || types.size() > 2 ||
          count_of(TokenType::Int) != 0 || count_of(TokenType::Long) != 0 ||
          types.empty()) {
        std::println("error:{}: Invalid type specifier", line);
        had_error_ = true;
        return Type::int_type();
      }
      const bool has_signed = count_of(TokenType::Signed) != 0;
      const bool has_unsigned = count_of(TokenType::Unsigned) != 0;
      if (has_signed && has_unsigned) {
        std::println("error:{}: Invalid type specifier", line);
        had_error_ = true;
        return Type::int_type();
      }
      if (has_signed)
        return Type::schar_type();
      if (has_unsigned)
        return Type::uchar_type();
      return Type::char_type();
    }
    bool has_int = false;
    bool has_long = false;
    bool has_signed = false;
    bool has_unsigned = false;
    bool invalid = types.empty();
    for (TokenType type : types) {
      bool *seen = type == TokenType::Int ? &has_int :
                   type == TokenType::Long ? &has_long :
                   type == TokenType::Signed ? &has_signed : &has_unsigned;
      if (*seen) invalid = true;
      *seen = true;
    }
    if (has_signed && has_unsigned) invalid = true;
    if (invalid) {
      std::println("error:{}: Invalid type specifier", line);
      had_error_ = true;
      return Type::int_type();
    }
    if (has_unsigned)
      return has_long ? Type::ulong_type() : Type::uint_type();
    return has_long ? Type::long_type() : Type::int_type();
  }

  Type parse_type_specifiers() {
    const uint32_t line = peek().line;
    std::vector<TokenType> types;
    while (check(TokenType::Int) || check(TokenType::Long) ||
           check(TokenType::Signed) || check(TokenType::Unsigned) ||
           check(TokenType::Double) || check(TokenType::Char) ||
           check(TokenType::Void))
      types.push_back(advance().type);
    return type_from_specifiers(types, line);
  }

  std::unique_ptr<Declarator> parse_declarator() {
    size_t pointer_count = 0;
    while (match(TokenType::Multiply))
      ++pointer_count;

    std::unique_ptr<Declarator> result;
    bool parenthesized = false;
    if (check(TokenType::Identifier)) {
      result = std::make_unique<Declarator>(
          Declarator{Declarator::Kind::Identifier, parse_identifier(), {},
                     {}});
    } else if (match(TokenType::LParen)) {
      result = parse_declarator();
      expect(TokenType::RParen, "\")\"");
      parenthesized = true;
    } else {
      std::println("error:{}: Expected declarator but found '{}'",
                   peek().line, peek().text);
      had_error_ = true;
      result = std::make_unique<Declarator>(
          Declarator{Declarator::Kind::Identifier, "__error", {}, {}});
    }

    while (true) {
      if (match(TokenType::LParen)) {
      std::vector<ParameterSyntax> params;
      if (check(TokenType::Void) && check(TokenType::RParen, 1)) {
        advance();
      } else if (!check(TokenType::RParen)) {
        do {
          if (check(TokenType::Static) || check(TokenType::Extern)) {
            std::println("error:{}: Storage-class specifier is not allowed on a parameter",
                         peek().line);
            had_error_ = true;
            advance();
          }
          Type base_type = parse_type_specifiers();
          params.push_back({std::move(base_type), parse_declarator()});
        } while (match(TokenType::Comma));
      }
      expect(TokenType::RParen, "\")\"");
      if (result->kind == Declarator::Kind::Array) {
        std::println("error:{}: Invalid combination of array and function declarators",
                     peek().line);
        had_error_ = true;
      }
      result = std::make_unique<Declarator>(Declarator{
          Declarator::Kind::Function, {}, std::move(result),
          std::move(params)});
      parenthesized = false;
      } else if (match(TokenType::LBracket)) {
        const Token size_token = peek();
        size_t array_size = 0;
        if (size_token.type == TokenType::FloatingConstant) {
          std::println("error:{}: Array size must be an integer constant",
                       size_token.line);
          had_error_ = true;
          advance();
        } else if (size_token.type == TokenType::Constant ||
                   size_token.type == TokenType::LongConstant ||
                   size_token.type == TokenType::UnsignedConstant ||
                   size_token.type == TokenType::UnsignedLongConstant ||
                   size_token.type == TokenType::CharConstant) {
          Exp constant = parse_constant();
          std::visit(
              Overload{
                  [&](const ConstInt &value) {
                    array_size = static_cast<size_t>(value.value);
                  },
                  [&](const ConstLong &value) {
                    if (value.value < 0 ||
                        static_cast<uint64_t>(value.value) >
                            std::numeric_limits<size_t>::max()) {
                      had_error_ = true;
                    } else {
                      array_size = static_cast<size_t>(value.value);
                    }
                  },
                  [&](const ConstUInt &value) { array_size = value.value; },
                  [&](const ConstULong &value) {
                    if (value.value > std::numeric_limits<size_t>::max())
                      had_error_ = true;
                    else
                      array_size = static_cast<size_t>(value.value);
                  },
                  [&](const auto &) { std::unreachable(); }},
              constant.value);
          if (std::holds_alternative<ConstInt>(constant.value) &&
              std::get<ConstInt>(constant.value).value < 0) {
            had_error_ = true;
          } else if (std::holds_alternative<ConstUInt>(constant.value) &&
                     std::get<ConstUInt>(constant.value).value == 0) {
            had_error_ = true;
          }
        } else {
          std::println("error:{}: Expected integer array size but found '{}'",
                       size_token.line, size_token.text);
          had_error_ = true;
          if (!check(TokenType::RBracket) && !check(TokenType::Eof))
            advance();
        }
        expect(TokenType::RBracket, "\"]\"");
        if (result->kind == Declarator::Kind::Function && !parenthesized) {
          std::println("error:{}: Invalid combination of array and function declarators",
                       peek().line);
          had_error_ = true;
        }
        result = std::make_unique<Declarator>(Declarator{
            Declarator::Kind::Array, {}, std::move(result), {}, array_size});
        parenthesized = false;
      } else {
        break;
      }
    }

    while (pointer_count-- > 0) {
      result = std::make_unique<Declarator>(Declarator{
          Declarator::Kind::Pointer, {}, std::move(result), {}});
    }
    return result;
  }

  ProcessedDeclarator process_declarator(const Declarator &declarator,
                                         Type base_type) {
    switch (declarator.kind) {
      case Declarator::Kind::Identifier:
        return {declarator.name, std::move(base_type), {}};
      case Declarator::Kind::Pointer:
        if (base_type.kind == TypeKind::Function) {
          std::println("error:{}: Pointers to functions are not supported",
                       peek().line);
          had_error_ = true;
        }
        return process_declarator(*declarator.inner,
                                  Type::pointer(std::move(base_type)));
      case Declarator::Kind::Array:
        return process_declarator(
            *declarator.inner,
            Type::array(std::move(base_type), declarator.array_size));
      case Declarator::Kind::Function: {
        if (declarator.inner->kind != Declarator::Kind::Identifier) {
          std::println("error:{}: Function pointers and functions returning functions are not supported",
                       peek().line);
          had_error_ = true;
          return {"__error", Type::int_type(), {}};
        }
        std::vector<Type> param_types;
        std::vector<std::string> param_names;
        for (const auto &param : declarator.params) {
          auto processed =
              process_declarator(*param.declarator, param.base_type);
          if (processed.type.kind == TypeKind::Function) {
            std::println("error:{}: Function parameters are not supported",
                         peek().line);
            had_error_ = true;
          }
          param_types.push_back(std::move(processed.type));
          param_names.push_back(std::move(processed.name));
        }
        return {declarator.inner->name,
                Type::function(std::move(param_types), std::move(base_type)),
                std::move(param_names)};
      }
    }
    std::unreachable();
  }

  struct AbstractDeclarator {
    enum class Kind { Base, Pointer, Array } kind = Kind::Base;
    std::unique_ptr<AbstractDeclarator> inner;
    size_t array_size = 0;
  };

  AbstractDeclarator parse_abstract_declarator() {
    size_t pointer_count = 0;
    while (match(TokenType::Multiply))
      ++pointer_count;

    AbstractDeclarator result;
    if (match(TokenType::LParen)) {
      result = parse_abstract_declarator();
      expect(TokenType::RParen, "\")\"");
    }
    while (match(TokenType::LBracket)) {
      const Token size_token = peek();
      Exp constant = parse_constant();
      size_t array_size = 0;
      std::visit(
          Overload{
              [&](const ConstInt &value) {
                if (value.value < 0)
                  had_error_ = true;
                else
                  array_size = static_cast<size_t>(value.value);
              },
              [&](const ConstLong &value) {
                if (value.value < 0)
                  had_error_ = true;
                else
                  array_size = static_cast<size_t>(value.value);
              },
              [&](const ConstUInt &value) { array_size = value.value; },
              [&](const ConstULong &value) {
                if (value.value > std::numeric_limits<size_t>::max())
                  had_error_ = true;
                else
                  array_size = static_cast<size_t>(value.value);
              },
              [&](const auto &) {
                std::println("error:{}: Array size must be an integer constant",
                             size_token.line);
                had_error_ = true;
              }},
          constant.value);
      expect(TokenType::RBracket, "\"]\"");
      result = AbstractDeclarator{
          AbstractDeclarator::Kind::Array,
          std::make_unique<AbstractDeclarator>(std::move(result)), array_size};
    }
    while (pointer_count-- > 0)
      result = AbstractDeclarator{AbstractDeclarator::Kind::Pointer,
                                  std::make_unique<AbstractDeclarator>(
                                      std::move(result)),
                                  0};
    return result;
  }

  static Type process_abstract_declarator(AbstractDeclarator declarator,
                                          Type base_type) {
    switch (declarator.kind) {
      case AbstractDeclarator::Kind::Base: return base_type;
      case AbstractDeclarator::Kind::Pointer:
        return process_abstract_declarator(
            std::move(*declarator.inner),
            Type::pointer(std::move(base_type)));
      case AbstractDeclarator::Kind::Array:
        return process_abstract_declarator(
            std::move(*declarator.inner),
            Type::array(std::move(base_type), declarator.array_size));
    }
    std::unreachable();
  }

  std::pair<Type, std::optional<StorageClass>> parse_specifiers() {
    std::vector<TokenType> types;
    std::optional<StorageClass> storage_class;
    uint32_t line = peek().line;

    while (check(TokenType::Int) || check(TokenType::Long) ||
           check(TokenType::Signed) || check(TokenType::Unsigned) ||
           check(TokenType::Double) || check(TokenType::Char) ||
           check(TokenType::Void) ||
           check(TokenType::Static) || check(TokenType::Extern)) {
      const Token specifier = advance();
      if (specifier.type == TokenType::Int ||
          specifier.type == TokenType::Long ||
          specifier.type == TokenType::Signed ||
          specifier.type == TokenType::Unsigned ||
          specifier.type == TokenType::Double ||
          specifier.type == TokenType::Char ||
          specifier.type == TokenType::Void) {
        types.push_back(specifier.type);
      } else if (storage_class) {
        std::println("error:{}: Invalid storage class", specifier.line);
        had_error_ = true;
      } else if (specifier.type == TokenType::Static) {
        storage_class = StorageClass{Static{}};
      } else {
        storage_class = StorageClass{Extern{}};
      }
    }

    Type declaration_type = type_from_specifiers(types, line);
    return {std::move(declaration_type), std::move(storage_class)};
  }

  Declaration parse_declaration() {
    uint32_t line = peek().line;
    auto [decl_type, storage_class] = parse_specifiers();
    auto declarator = parse_declarator();
    auto processed = process_declarator(*declarator, std::move(decl_type));

    if (processed.type.kind == TypeKind::Function) {
      std::unique_ptr<Block> body;
      if (check(TokenType::LBrace)) {
        body = std::make_unique<Block>(parse_block());
      } else {
        expect(TokenType::Semicolon, "\";\"");
      }
      return FunDecl{FunctionDeclaration{
          std::move(processed.name), std::move(processed.param_names),
          std::move(body), line,
          std::move(storage_class),
          std::move(processed.type)}};
    }

    std::optional<Initializer> init;
    if (match(TokenType::Assign)) init = parse_initializer();
    expect(TokenType::Semicolon, "\";\"");
    return VarDecl{VariableDeclaration{
        std::move(processed.name), std::move(init), line,
        std::move(storage_class), std::move(processed.type)}};
  }

  bool is_declaration_start() const {
    return check(TokenType::Int) || check(TokenType::Long) ||
           check(TokenType::Signed) || check(TokenType::Unsigned) ||
           check(TokenType::Double) || check(TokenType::Char) ||
           check(TokenType::Void) ||
           check(TokenType::Static) ||
           check(TokenType::Extern);
  }

  BlockItem parse_block_item() {
    return is_declaration_start() ? BlockItem{parse_declaration()}
                                 : BlockItem{parse_statement()};
  }

  Block parse_block() {
    expect(TokenType::LBrace, "\"{\"");

    std::vector<BlockItem> items;
    while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
      items.push_back(parse_block_item());
    }
    expect(TokenType::RBrace, "\"}\"");

    return {std::move(items)};
  }

  Statement parse_compound_stmt() {
    uint32_t line = peek().line;
    return Statement{Compound{std::make_unique<Block>(parse_block()), line}};
  }

  Program parse_program() {
    std::vector<Declaration> declarations;

    while (!check(TokenType::Eof)) {
      if (!is_declaration_start()) {
        std::println("error:{}: Expected declaration but found '{}'", peek().line, peek().text);
        had_error_ = true;
        advance();
        continue;
      }
      declarations.push_back(parse_declaration());
    }

    return {std::move(declarations)};
  }

public:
  Parser(const std::vector<Token>& tokens, std::string_view filename)
      : tokens_(tokens), filename_(filename) {}

  std::optional<Program> parse() {
    auto program = parse_program();
    if (had_error_) return std::nullopt;
    return program;
  }

  bool ok() const { return !had_error_; }
};