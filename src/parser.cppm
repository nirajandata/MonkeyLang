module;

#include <charconv>
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
  Variant result;
  bool matched = false;
  ([&] {
    if (!matched) {
      using Alt = std::variant_alternative_t<Is, Variant>;
      if (type == get_token_type<Alt>()) {
        result = Alt{};
        matched = true;
      }
    }
  }(), ...);
  if (!matched) std::unreachable();
  return result;
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
  return type == TokenType::Increment ? IncDecOp{Increment{}}
                                       : IncDecOp{Decrement{}};
}

export class Parser {
  const std::vector<Token>& tokens_;
  size_t pos_ = 0;
  bool had_error_ = false;
  std::string_view filename_;

  const Token& peek(size_t offset = 0) const { return tokens_[pos_ + offset]; }

  Token advance() { return tokens_[pos_++]; }

  bool check(TokenType type) const { return peek().type == type; }

  void expect(TokenType type, std::string_view expected) {
    if (!check(type)) {
      std::println("error:{}: Expected {} but found '{}'", peek().line, expected, peek().text);
      had_error_ = true;
    } else {
      advance();
    }
  }

  Constant parse_constant() {
    const Token& tok = advance();
    if (tok.type != TokenType::Constant) {
      std::println("error:{}: Expected constant but found '{}'", tok.line, tok.text);
      had_error_ = true;
      return {0, tok.line};
    }

    int32_t value = 0;
    const auto [ptr, ec] = std::from_chars(tok.text.data(), tok.text.data() + tok.text.size(), value);

    if (ec != std::errc{} || ptr != tok.text.data() + tok.text.size()) {
      std::println("error:{}: '{}' is not a valid integer constant", tok.line, tok.text);
      had_error_ = true;
      return {0, tok.line};
    }
    return {value, tok.line};
  }

  Exp parse_primary() {
    auto tok = peek();

    if (tok.type == TokenType::Constant) {
      return Exp{parse_constant()};
    }

    if (tok.type == TokenType::Identifier) {
      advance();
      return Exp{Var{std::string(tok.text), tok.line}};
    }

    if (tok.type == TokenType::Increment || tok.type == TokenType::Decrement) {
      advance();
      return Exp{IncDec{inc_dec_op(tok.type),
                         std::make_unique<Exp>(parse_postfix()), false,
                         tok.line}};
    }

    if (tok.type == TokenType::Complement || tok.type == TokenType::Subtract || tok.type == TokenType::Not) {
      advance();
      UnaryOp op = (tok.type == TokenType::Complement) ? UnaryOp{Complement{}}
                 : (tok.type == TokenType::Subtract) ? UnaryOp{Negate{}}
                 : UnaryOp{Not{}};
      return Exp{Unary{std::move(op), std::make_unique<Exp>(parse_postfix()), tok.line}};
    }

    if (tok.type == TokenType::LParen) {
      advance();
      auto inner = parse_exp(0);
      expect(TokenType::RParen, "\")\"");
      return inner;
    }

    std::println("error:{}: Expected expression but found '{}'", tok.line, tok.text);
    had_error_ = true;
    advance();
    return Exp{Constant{0, tok.line}};
  }

  Exp parse_postfix() {
    auto exp = parse_primary();
    while (check(TokenType::Increment) || check(TokenType::Decrement)) {
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
    auto val = parse_exp(0);
    expect(TokenType::Semicolon, "\";\"");
    return {std::move(val), line};
  }


  std::optional<Exp> parse_optional_exp(TokenType end) {
    if (check(end) || check(TokenType::Eof)) return std::nullopt;
    return parse_exp(0);
  }

  static std::optional<Expression> as_statement_exp(std::optional<Exp> exp,
                                                    uint32_t line) {
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
    auto condition =
        std::make_unique<Expression>(parse_exp(0), condition_line);
    expect(TokenType::RParen, "\")\"");
    auto body = std::make_unique<Statement>(parse_statement());
    return Statement{While{std::move(condition), std::move(body), {}, {},
                          line}};
  }

  Statement parse_do_while() {
    uint32_t line = peek().line;
    expect(TokenType::Do, "\"do\"");
    auto body = std::make_unique<Statement>(parse_statement());
    expect(TokenType::While, "\"while\"");
    expect(TokenType::LParen, "\"(\"");
    uint32_t condition_line = peek().line;
    auto condition =
        std::make_unique<Expression>(parse_exp(0), condition_line);
    expect(TokenType::RParen, "\")\"");
    expect(TokenType::Semicolon, "\";\"");
    return Statement{DoWhile{std::move(body), std::move(condition), {}, {},
                             line}};
  }

  ForInit parse_for_init() {
    if (check(TokenType::Int)) return ForInit{InitDecl{parse_declaration()}};
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
    auto condition =
        as_statement_exp(parse_optional_exp(TokenType::Semicolon), condition_line);
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
    return Statement{Switch{std::move(condition), std::move(body), {}, {},
                            nullptr, line}};
  }

  Statement parse_if() {
    uint32_t line = peek().line;
    expect(TokenType::If, "\"if\"");
    expect(TokenType::LParen, "\"(\"");
    auto condition = parse_exp(0);
    expect(TokenType::RParen, "\")\"");

    auto then_stmt = std::make_unique<Statement>(parse_statement());

    std::unique_ptr<Statement> else_stmt;
    if (check(TokenType::Else)) {
      advance();
      else_stmt = std::make_unique<Statement>(parse_statement());
    }

    return Statement{If{std::move(condition), std::move(then_stmt),
                       std::move(else_stmt), line}};
  }

  Statement parse_goto() {
    uint32_t line = peek().line;
    expect(TokenType::Goto, "\"goto\"");

    const Token& name_tok = advance();
    if (name_tok.type != TokenType::Identifier) {
      std::println("error:{}: Expected identifier but found '{}'", name_tok.line, name_tok.text);
      had_error_ = true;
    }
    std::string name(name_tok.text);

    expect(TokenType::Semicolon, "\";\"");
    return Statement{Goto{std::move(name), line}};
  }

  Statement parse_label() {
    uint32_t line = peek().line;

    const Token& name_tok = advance();
    if (name_tok.type != TokenType::Identifier) {
      std::println("error:{}: Expected identifier but found '{}'", name_tok.line, name_tok.text);
      had_error_ = true;
    }
    std::string name(name_tok.text);

    expect(TokenType::Colon, "\":\"");

    auto stmt = std::make_unique<Statement>(parse_statement());
    return Statement{Label{std::move(name), std::move(stmt), line}};
  }

  Statement parse_statement() {
    if (check(TokenType::Return)) {
      return Statement{parse_return()};
    }

    if (check(TokenType::If)) {
      return parse_if();
    }

    if (check(TokenType::Break)) {
      return parse_break();
    }

    if (check(TokenType::Continue)) {
      return parse_continue();
    }

    if (check(TokenType::Case)) {
      return parse_case();
    }

    if (check(TokenType::Default)) {
      return parse_default();
    }

    if (check(TokenType::Switch)) {
      return parse_switch();
    }

    if (check(TokenType::While)) {
      return parse_while();
    }

    if (check(TokenType::Do)) {
      return parse_do_while();
    }

    if (check(TokenType::For)) {
      return parse_for();
    }

    if (check(TokenType::Goto)) {
      return parse_goto();
    }

    if (check(TokenType::Identifier) && peek(1).type == TokenType::Colon) {
      return parse_label();
    }

    if (check(TokenType::LBrace)) {
      return parse_compound_stmt();
    }

    if (check(TokenType::Semicolon)) {
      advance();
      return Statement{Null{}};
    }

    uint32_t line = peek().line;
    auto exp = parse_exp(0);
    expect(TokenType::Semicolon, "\";\"");
    return Statement{Expression{std::move(exp), line}};
  }

  Declaration parse_declaration() {
    uint32_t line = peek().line;
    expect(TokenType::Int, "\"int\"");

    const Token& name_tok = advance();
    if (name_tok.type != TokenType::Identifier) {
      std::println("error:{}: Expected identifier but found '{}'", name_tok.line, name_tok.text);
      had_error_ = true;
    }
    std::string name(name_tok.text);

    std::optional<Exp> init;
    if (check(TokenType::Assign)) {
      advance();
      init = parse_exp(0);
    }

    expect(TokenType::Semicolon, "\";\"");
    return {std::move(name), std::move(init), line};
  }

  BlockItem parse_block_item() {
    if (check(TokenType::Int)) {
      return parse_declaration();
    }
    return parse_statement();
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

  Function parse_function() {
    uint32_t line = peek().line;
    expect(TokenType::Int, "\"int\"");

    const Token& name_tok = advance();
    if (name_tok.type != TokenType::Identifier) {
      std::println("error:{}: Expected identifier but found '{}'", name_tok.line, name_tok.text);
      had_error_ = true;
    }

    std::string name(name_tok.text);
    expect(TokenType::LParen, "\"(\"");
    expect(TokenType::Void, "\"void\"");
    expect(TokenType::RParen, "\")\"");

    auto body = parse_block();

    return {std::move(name), std::move(body), line};
  }

  Program parse_program() {
    auto func = parse_function();

    if (!check(TokenType::Eof)) {
      std::println("error:{}: Expected end of file but found '{}'", peek().line, peek().text);
      had_error_ = true;
    }

    return {std::move(func)};
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