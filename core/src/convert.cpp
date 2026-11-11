#include "s2a/convert.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

namespace s2a::convert {

namespace {

using project::ProgramLanguage;

// --- The program, whatever the language ------------------------------------------------------

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr {
    enum class Kind { Number, String, Bool, Name, Unary, Binary, Call, Ternary, List };
    Kind kind = Kind::Number;
    /// Number text, string value, name, operator ("+", "and", "not", "-"...) or called function.
    std::string text;
    std::vector<ExprPtr> items; ///< Operands, arguments, list items; Ternary: condition, then, else.
};

ExprPtr make(Expr::Kind kind, std::string text, std::vector<ExprPtr> items = {}) {
    auto expr = std::make_shared<Expr>();
    expr->kind = kind;
    expr->text = std::move(text);
    expr->items = std::move(items);
    return expr;
}

struct Stmt;
using Body = std::vector<Stmt>;

struct Stmt {
    enum class Kind {
        Expression, ///< value
        Assign,     ///< name op value ("=", "+=", "-=", "*=", "/=", "%=")
        Declare,    ///< type name = value (value may be null); constant
        If,         ///< conditions[i] -> bodies[i]; one more body: else
        While,      ///< value, bodies[0]
        ForRange,   ///< name in range(start, end, step), bodies[0]
        ForEach,    ///< name in list value, bodies[0]
        Return,     ///< value or null
        Break,
        Continue,
        Comment, ///< name: the text
        Blank,
        Directive, ///< name: a C++ line (#include...)
        Unknown,   ///< name: the source lines, kept as a comment
        Function,  ///< name(params) -> type, bodies[0]
        Sequence,  ///< bodies[0], written in place (the start of a for loop and its while loop)
    };
    Kind kind = Kind::Blank;
    std::string name;
    std::string op;
    std::string type; ///< C++ type when known (declarations, functions)
    bool constant = false;
    ExprPtr value;
    std::vector<ExprPtr> conditions;
    std::vector<Body> bodies;
    std::vector<std::pair<std::string, std::string>> params; ///< (name, C++ type or "")
    ExprPtr start, end, step;
    int line = 0;
};

// --- Tokens ------------------------------------------------------------------------------------

struct Token {
    enum class Type { Number, String, Name, Op, Comment, Directive, Newline, Indent, Dedent, End };
    Type type = Type::End;
    std::string text;
    int line = 0;
};

bool nameStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

bool nameChar(char c) {
    return nameStart(c) || std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool digit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

const std::vector<std::string> kOperators{"===", "!==", "**=", "//=", "==", "!=", "<=", ">=", "&&", "||", "++", "--",
                                          "+=",  "-=",  "*=",  "/=",  "%=", "**", "//", "<<", ">>", "->", "::"};

/// Reads a string literal starting at `i` (its quote); returns its value and moves `i` after it.
std::string readString(std::string_view source, std::size_t& i) {
    const char quote = source[i++];
    std::string value;
    while (i < source.size() && source[i] != quote && source[i] != '\n') {
        if (source[i] == '\\' && i + 1 < source.size()) {
            const char next = source[i + 1];
            value += next == 'n' ? '\n' : next == 't' ? '\t' : next;
            i += 2;
            continue;
        }
        value += source[i++];
    }
    ++i;
    return value;
}

/// Reads a number at `i`: digits, a dot, an exponent; C++ suffixes (f, L, UL) are dropped.
std::string readNumber(std::string_view source, std::size_t& i) {
    std::string number;
    if (source.substr(i, 2) == "0x" || source.substr(i, 2) == "0X") {
        number = "0x";
        i += 2;
        while (i < source.size() && std::isxdigit(static_cast<unsigned char>(source[i])) != 0) {
            number += source[i++];
        }
        return number;
    }
    while (i < source.size()) {
        const char c = source[i];
        if (digit(c) || c == '.') {
            number += c;
            ++i;
        } else if ((c == 'e' || c == 'E') && i + 1 < source.size() && (digit(source[i + 1]) || source[i + 1] == '-')) {
            number += c;
            number += source[i + 1];
            i += 2;
        } else {
            break;
        }
    }
    while (i < source.size() && std::string_view("fFlLuU").find(source[i]) != std::string_view::npos) {
        ++i;
    }
    return number;
}

/// Reads an operator at `i` (the longest known one, else one character).
std::string readOperator(std::string_view source, std::size_t& i) {
    for (const std::string& op : kOperators) {
        if (source.substr(i, op.size()) == op) {
            i += op.size();
            return op;
        }
    }
    return std::string(1, source[i++]);
}

std::size_t lineEnd(std::string_view source, std::size_t from) {
    return std::min(source.find('\n', from), source.size());
}

/// C++ and JavaScript.
std::vector<Token> lexC(std::string_view source) {
    std::vector<Token> tokens;
    int line = 1;
    bool lineStart = true;
    std::size_t i = 0;
    while (i < source.size()) {
        const char c = source[i];
        if (c == '\n') {
            ++line;
            ++i;
            lineStart = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '#' && lineStart) {
            const std::size_t end = lineEnd(source, i);
            tokens.push_back({Token::Type::Directive, trim(std::string(source.substr(i, end - i))), line});
            i = end;
            continue;
        }
        lineStart = false;
        if (source.substr(i, 2) == "//") {
            const std::size_t end = lineEnd(source, i);
            tokens.push_back({Token::Type::Comment, trim(std::string(source.substr(i + 2, end - i - 2))), line});
            i = end;
            continue;
        }
        if (source.substr(i, 2) == "/*") {
            const std::size_t end = std::min(source.find("*/", i + 2), source.size());
            std::istringstream lines(std::string(source.substr(i + 2, end - i - 2)));
            std::string text;
            int at = line;
            while (std::getline(lines, text)) {
                text = trim(text);
                if (!text.empty() && text[0] == '*') {
                    text = trim(text.substr(1));
                }
                if (!text.empty()) {
                    tokens.push_back({Token::Type::Comment, text, at});
                }
                ++at;
            }
            line += static_cast<int>(std::count(source.begin() + static_cast<std::ptrdiff_t>(i),
                                                source.begin() + static_cast<std::ptrdiff_t>(end), '\n'));
            i = std::min(end + 2, source.size());
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            tokens.push_back({Token::Type::String, readString(source, i), line});
            continue;
        }
        if (digit(c) || (c == '.' && i + 1 < source.size() && digit(source[i + 1]))) {
            tokens.push_back({Token::Type::Number, readNumber(source, i), line});
            continue;
        }
        if (nameStart(c)) {
            std::string name;
            while (i < source.size() && nameChar(source[i])) {
                name += source[i++];
            }
            tokens.push_back({Token::Type::Name, name, line});
            continue;
        }
        tokens.push_back({Token::Type::Op, readOperator(source, i), line});
    }
    tokens.push_back({Token::Type::End, "", line});
    return tokens;
}

/// Python: Newline at the end of each logical line, Indent / Dedent when the indentation changes.
std::vector<Token> lexPython(std::string_view source) {
    std::vector<Token> tokens;
    std::vector<int> indents{0};
    int line = 1;
    int depth = 0; // open brackets: the line goes on
    std::size_t i = 0;
    bool lineStart = true;
    while (i < source.size()) {
        if (lineStart && depth == 0) {
            int width = 0;
            while (i < source.size() && (source[i] == ' ' || source[i] == '\t')) {
                width += source[i] == '\t' ? 4 : 1;
                ++i;
            }
            if (i >= source.size()) {
                break;
            }
            if (source[i] == '\r') {
                ++i;
                continue;
            }
            if (source[i] == '\n') { // blank line
                ++i;
                ++line;
                continue;
            }
            if (source[i] == '#') { // a comment line: no indentation change
                const std::size_t end = lineEnd(source, i);
                tokens.push_back({Token::Type::Comment, trim(std::string(source.substr(i + 1, end - i - 1))), line});
                i = end;
                continue;
            }
            if (width > indents.back()) {
                indents.push_back(width);
                tokens.push_back({Token::Type::Indent, "", line});
            }
            while (width < indents.back()) {
                indents.pop_back();
                tokens.push_back({Token::Type::Dedent, "", line});
            }
            lineStart = false;
        }
        const char c = source[i];
        if (c == '\n') {
            if (depth == 0) {
                tokens.push_back({Token::Type::Newline, "", line});
                lineStart = true;
            }
            ++line;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '\\' && i + 1 < source.size() && source[i + 1] == '\n') {
            i += 2;
            ++line;
            continue;
        }
        if (c == '#') {
            const std::size_t end = lineEnd(source, i);
            tokens.push_back({Token::Type::Comment, trim(std::string(source.substr(i + 1, end - i - 1))), line});
            i = end;
            continue;
        }
        if (c == '"' || c == '\'') {
            tokens.push_back({Token::Type::String, readString(source, i), line});
            continue;
        }
        if (digit(c) || (c == '.' && i + 1 < source.size() && digit(source[i + 1]))) {
            tokens.push_back({Token::Type::Number, readNumber(source, i), line});
            continue;
        }
        if (nameStart(c)) {
            std::string name;
            while (i < source.size() && nameChar(source[i])) {
                name += source[i++];
            }
            tokens.push_back({Token::Type::Name, name, line});
            continue;
        }
        const std::string op = readOperator(source, i);
        if (op == "(" || op == "[" || op == "{") {
            ++depth;
        } else if ((op == ")" || op == "]" || op == "}") && depth > 0) {
            --depth;
        }
        tokens.push_back({Token::Type::Op, op, line});
    }
    if (!lineStart) {
        tokens.push_back({Token::Type::Newline, "", line});
    }
    while (indents.size() > 1) {
        indents.pop_back();
        tokens.push_back({Token::Type::Dedent, "", line});
    }
    tokens.push_back({Token::Type::End, "", line});
    return tokens;
}

// --- Reading -------------------------------------------------------------------------------------

struct ParseError {};

const std::set<std::string> kCppTypes{"int",     "long",    "float",    "double",  "bool",     "boolean", "byte",
                                      "char",    "short",   "void",     "String",  "word",     "size_t",  "uint8_t",
                                      "int16_t", "uint16_t", "int32_t", "uint32_t", "unsigned", "signed"};
const std::set<std::string> kCppModifiers{"const", "static", "volatile", "constexpr", "inline"};

class Parser {
public:
    Parser(std::vector<Token> tokens, ProgramLanguage language, std::string_view source)
        : tokens_(std::move(tokens)), python_(language == ProgramLanguage::Python),
          cpp_(language == ProgramLanguage::Cpp) {
        std::istringstream lines{std::string(source)};
        std::string text;
        while (std::getline(lines, text)) {
            lines_.push_back(text);
        }
    }

    Body program() {
        Body body;
        while (peek().type != Token::Type::End) {
            const Token::Type type = peek().type;
            if (python_ && (type == Token::Type::Newline || type == Token::Type::Dedent || type == Token::Type::Indent)) {
                ++at_;
                continue;
            }
            if (!python_ && isOp("}")) { // a closing brace without its opening one
                body.push_back(unknownToken());
                continue;
            }
            statementInto(body, true);
        }
        return body;
    }

private:
    const Token& peek(std::size_t ahead = 0) const { return tokens_[std::min(at_ + ahead, tokens_.size() - 1)]; }
    const Token& next() {
        const Token& token = peek();
        if (at_ < tokens_.size() - 1) {
            ++at_;
        }
        return token;
    }
    bool isOp(const std::string& op, std::size_t ahead = 0) const {
        return peek(ahead).type == Token::Type::Op && peek(ahead).text == op;
    }
    bool isName(const std::string& name, std::size_t ahead = 0) const {
        return peek(ahead).type == Token::Type::Name && peek(ahead).text == name;
    }
    bool accept(const std::string& op) {
        if (isOp(op)) {
            ++at_;
            return true;
        }
        return false;
    }
    void expect(const std::string& op) {
        if (!accept(op)) {
            throw ParseError{};
        }
    }
    std::string name() {
        if (peek().type != Token::Type::Name) {
            throw ParseError{};
        }
        return next().text;
    }

    /// Adds the next statement (with a blank line before it when the source has one).
    void statementInto(Body& body, bool topLevel) {
        const int line = peek().line;
        if (!body.empty() && line > lastLine_ + 1 && body.back().kind != Stmt::Kind::Blank) {
            body.push_back(Stmt{});
        }
        const std::size_t start = at_;
        try {
            Stmt stmt = python_ ? pythonStatement(topLevel) : cStatement(topLevel);
            if (stmt.line == 0) {
                stmt.line = line;
            }
            if (stmt.kind != Stmt::Kind::Blank) {
                body.push_back(std::move(stmt));
            }
        } catch (const ParseError&) {
            at_ = start;
            body.push_back(unknown(line));
        }
        // The last line read (indentation tokens carry the line of what follows them).
        for (std::size_t i = at_; i > start; --i) {
            const Token::Type type = tokens_[i - 1].type;
            if (type != Token::Type::Indent && type != Token::Type::Dedent) {
                lastLine_ = tokens_[i - 1].line;
                break;
            }
        }
        if (at_ == start) { // nothing read: never loop forever
            body.push_back(unknownToken());
        }
    }

    Stmt unknownToken() {
        Stmt stmt;
        stmt.kind = Stmt::Kind::Unknown;
        stmt.line = peek().line;
        stmt.name = sourceLines(stmt.line, stmt.line);
        next();
        return stmt;
    }

    std::string sourceLines(int first, int last) const {
        std::string text;
        for (int i = first; i <= last && i <= static_cast<int>(lines_.size()); ++i) {
            text += (text.empty() ? "" : "\n") + lines_[static_cast<std::size_t>(i - 1)];
        }
        return text;
    }

    /// Skips a statement that could not be read and keeps its source lines.
    Stmt unknown(int line) {
        int last = line;
        if (python_) {
            int depth = 0;
            while (peek().type != Token::Type::End) {
                const Token& token = next();
                last = std::max(last, token.line);
                if (token.type == Token::Type::Indent) {
                    ++depth;
                } else if (token.type == Token::Type::Dedent) {
                    if (depth == 0) {
                        --at_;
                        break;
                    }
                    if (--depth == 0 && peek().type != Token::Type::Indent) {
                        break;
                    }
                } else if (token.type == Token::Type::Newline && depth == 0 && peek().type != Token::Type::Indent) {
                    break;
                }
            }
        } else {
            int depth = 0;
            while (peek().type != Token::Type::End) {
                if (isOp("}") && depth == 0) {
                    break;
                }
                const Token& token = next();
                last = std::max(last, token.line);
                if (token.type == Token::Type::Op && token.text == "{") {
                    ++depth;
                } else if (token.type == Token::Type::Op && token.text == "}") {
                    if (--depth == 0 && !isOp(";")) {
                        break;
                    }
                } else if (token.type == Token::Type::Op && token.text == ";" && depth == 0) {
                    break;
                }
            }
        }
        Stmt stmt;
        stmt.kind = Stmt::Kind::Unknown;
        stmt.line = line;
        stmt.name = sourceLines(line, last);
        return stmt;
    }

    // --- Expressions (both syntaxes) ---

    ExprPtr expression() { return ternary(); }

    ExprPtr ternary() {
        ExprPtr value = logicalOr();
        if (python_ && isName("if")) {
            next();
            ExprPtr condition = logicalOr();
            if (!isName("else")) {
                throw ParseError{};
            }
            next();
            return make(Expr::Kind::Ternary, "?", {condition, value, ternary()});
        }
        if (!python_ && accept("?")) {
            ExprPtr yes = ternary();
            expect(":");
            return make(Expr::Kind::Ternary, "?", {value, yes, ternary()});
        }
        return value;
    }

    ExprPtr logicalOr() {
        ExprPtr left = logicalAnd();
        while (python_ ? isName("or") : isOp("||")) {
            next();
            left = make(Expr::Kind::Binary, "or", {left, logicalAnd()});
        }
        return left;
    }

    ExprPtr logicalAnd() {
        ExprPtr left = logicalNot();
        while (python_ ? isName("and") : isOp("&&")) {
            next();
            left = make(Expr::Kind::Binary, "and", {left, logicalNot()});
        }
        return left;
    }

    ExprPtr logicalNot() {
        if (python_ && isName("not")) {
            next();
            return make(Expr::Kind::Unary, "not", {logicalNot()});
        }
        return comparison();
    }

    ExprPtr comparison() {
        ExprPtr left = additive();
        for (;;) {
            if (peek().type != Token::Type::Op) {
                return left;
            }
            std::string op = peek().text;
            if (op == "===") {
                op = "==";
            } else if (op == "!==") {
                op = "!=";
            }
            if (op != "==" && op != "!=" && op != "<" && op != ">" && op != "<=" && op != ">=") {
                return left;
            }
            next();
            left = make(Expr::Kind::Binary, op, {left, additive()});
        }
    }

    ExprPtr additive() {
        ExprPtr left = multiplicative();
        while (isOp("+") || isOp("-")) {
            const std::string op = next().text;
            left = make(Expr::Kind::Binary, op, {left, multiplicative()});
        }
        return left;
    }

    ExprPtr multiplicative() {
        ExprPtr left = unary();
        while (isOp("*") || isOp("/") || isOp("%") || (python_ && isOp("//"))) {
            const std::string op = next().text;
            left = make(Expr::Kind::Binary, op, {left, unary()});
        }
        return left;
    }

    ExprPtr unary() {
        if (isOp("-") || isOp("+")) {
            const std::string op = next().text;
            ExprPtr operand = unary();
            return op == "-" ? make(Expr::Kind::Unary, "-", {operand}) : operand;
        }
        if (!python_ && isOp("!")) {
            next();
            return make(Expr::Kind::Unary, "not", {unary()});
        }
        // C++ cast: (int) x, (float)(x), (unsigned long) x
        if (cpp_ && isOp("(") && peek(1).type == Token::Type::Name && kCppTypes.count(peek(1).text) != 0) {
            std::size_t close = 2;
            while (peek(close).type == Token::Type::Name && kCppTypes.count(peek(close).text) != 0) {
                ++close;
            }
            if (isOp(")", close)) {
                next();
                const std::string type = next().text;
                at_ += close - 2;
                next(); // )
                const bool integer = type != "float" && type != "double";
                return make(Expr::Kind::Call, integer ? "int" : "float", {unary()});
            }
        }
        return power();
    }

    ExprPtr power() {
        ExprPtr base = postfix();
        if (isOp("**")) {
            next();
            return make(Expr::Kind::Call, "pow", {base, unary()});
        }
        return base;
    }

    ExprPtr postfix() {
        ExprPtr value = primary();
        for (;;) {
            if (isOp("(")) {
                if (value->kind != Expr::Kind::Name) {
                    throw ParseError{};
                }
                next();
                std::vector<ExprPtr> args;
                if (!isOp(")")) {
                    do {
                        args.push_back(expression());
                    } while (accept(","));
                }
                expect(")");
                value = call(value->text, std::move(args));
            } else if (isOp(".") && peek(1).type == Token::Type::Name && value->kind == Expr::Kind::Name) {
                next();
                value = make(Expr::Kind::Name, value->text + "." + next().text);
            } else {
                return value;
            }
        }
    }

    /// A call, with the names of one language turned into the common ones.
    static ExprPtr call(std::string name, std::vector<ExprPtr> args) {
        static const std::map<std::string, std::string> common{
            {"Serial.println", "print"}, {"Serial.print", "print"},  {"console.log", "print"},
            {"Math.abs", "abs"},         {"Math.min", "min"},        {"Math.max", "max"},
            {"Math.floor", "floor"},     {"Math.round", "round"},    {"Math.sqrt", "sqrt"},
            {"Math.pow", "pow"},         {"math.floor", "floor"},    {"math.sqrt", "sqrt"},
            {"math.pow", "pow"},         {"Math.trunc", "int"},      {"Number", "float"},
            {"parseInt", "int"},         {"parseFloat", "float"}};
        if (const auto found = common.find(name); found != common.end()) {
            name = found->second;
        }
        if (name == "time.sleep" && args.size() == 1) {
            return make(Expr::Kind::Call, "delay",
                        {make(Expr::Kind::Binary, "*", {args[0], make(Expr::Kind::Number, "1000")})});
        }
        return make(Expr::Kind::Call, name, std::move(args));
    }

    ExprPtr primary() {
        const Token& token = peek();
        if (token.type == Token::Type::Number) {
            return make(Expr::Kind::Number, next().text);
        }
        if (token.type == Token::Type::String) {
            return make(Expr::Kind::String, next().text);
        }
        if (token.type == Token::Type::Name) {
            static const std::set<std::string> unsupported{"None",     "null", "undefined", "lambda", "new",
                                                           "function", "def",  "class",     "this",   "await"};
            const std::string word = next().text;
            if (word == "true" || word == "True") {
                return make(Expr::Kind::Bool, "true");
            }
            if (word == "false" || word == "False") {
                return make(Expr::Kind::Bool, "false");
            }
            if (unsupported.count(word) != 0) {
                throw ParseError{};
            }
            return make(Expr::Kind::Name, word);
        }
        if (accept("(")) {
            ExprPtr inner = expression();
            if (python_ && isOp(",")) { // tuple: a list of values
                std::vector<ExprPtr> items{inner};
                while (accept(",") && !isOp(")")) {
                    items.push_back(expression());
                }
                expect(")");
                return make(Expr::Kind::List, "", std::move(items));
            }
            expect(")");
            return inner;
        }
        if (isOp("[") || (cpp_ && isOp("{"))) {
            const std::string close = next().text == "[" ? "]" : "}";
            std::vector<ExprPtr> items;
            if (!isOp(close)) {
                do {
                    items.push_back(expression());
                } while (accept(",") && !isOp(close));
            }
            expect(close);
            return make(Expr::Kind::List, "", std::move(items));
        }
        throw ParseError{};
    }

    // --- C++ and JavaScript statements ---

    Body cBlock() {
        Body body;
        if (!accept("{")) {
            statementInto(body, false); // one statement without braces
            return body;
        }
        while (!isOp("}") && peek().type != Token::Type::End) {
            statementInto(body, false);
        }
        expect("}");
        return body;
    }

    /// A C++ type at the current token ("unsigned long", "const int"...): reads and returns it, or "".
    std::string cType(bool& constant) {
        std::size_t ahead = 0;
        std::string type;
        constant = false;
        while (peek(ahead).type == Token::Type::Name &&
               (kCppModifiers.count(peek(ahead).text) != 0 || kCppTypes.count(peek(ahead).text) != 0)) {
            const std::string& word = peek(ahead).text;
            if (word == "const" || word == "constexpr") {
                constant = true;
            } else if (kCppTypes.count(word) != 0) {
                type += (type.empty() ? "" : " ") + word;
            }
            ++ahead;
        }
        if (type.empty() || peek(ahead).type != Token::Type::Name) {
            return {};
        }
        at_ += ahead;
        return type;
    }

    Stmt cStatement(bool topLevel) {
        (void)topLevel;
        Stmt stmt;
        const Token& token = peek();
        if (token.type == Token::Type::Comment) {
            stmt.kind = Stmt::Kind::Comment;
            stmt.name = next().text;
            return stmt;
        }
        if (token.type == Token::Type::Directive) {
            stmt.kind = Stmt::Kind::Directive;
            stmt.name = next().text;
            return stmt;
        }
        if (accept(";")) {
            return stmt; // Blank
        }
        if (token.type == Token::Type::Name) {
            const std::string word = token.text;
            if (word == "if") {
                next();
                stmt.kind = Stmt::Kind::If;
                for (;;) {
                    expect("(");
                    stmt.conditions.push_back(expression());
                    expect(")");
                    stmt.bodies.push_back(cBlock());
                    if (!isName("else")) {
                        break;
                    }
                    next();
                    if (!isName("if")) {
                        stmt.bodies.push_back(cBlock());
                        break;
                    }
                    next();
                }
                return stmt;
            }
            if (word == "while") {
                next();
                stmt.kind = Stmt::Kind::While;
                expect("(");
                stmt.value = expression();
                expect(")");
                stmt.bodies.push_back(cBlock());
                return stmt;
            }
            if (word == "for") {
                return cFor();
            }
            if (word == "return") {
                next();
                stmt.kind = Stmt::Kind::Return;
                if (!isOp(";")) {
                    stmt.value = expression();
                }
                expect(";");
                return stmt;
            }
            if (word == "break" || word == "continue") {
                next();
                stmt.kind = word == "break" ? Stmt::Kind::Break : Stmt::Kind::Continue;
                expect(";");
                return stmt;
            }
            if (word == "function" && !cpp_) {
                next();
                stmt.kind = Stmt::Kind::Function;
                stmt.name = name();
                expect("(");
                if (!isOp(")")) {
                    do {
                        stmt.params.emplace_back(name(), "");
                    } while (accept(","));
                }
                expect(")");
                stmt.bodies.push_back(cBlock());
                return stmt;
            }
            if (!cpp_ && (word == "let" || word == "const" || word == "var")) {
                next();
                stmt.kind = Stmt::Kind::Declare;
                stmt.constant = word == "const";
                stmt.name = name();
                if (accept("=")) {
                    stmt.value = expression();
                }
                expect(";");
                return stmt;
            }
            if (cpp_) {
                bool constant = false;
                const std::size_t start = at_;
                const std::string type = cType(constant);
                if (!type.empty()) {
                    const std::string declared = name();
                    if (isOp("(")) { // function definition (or prototype)
                        next();
                        stmt.kind = Stmt::Kind::Function;
                        stmt.name = declared;
                        stmt.type = type;
                        if (isName("void") && isOp(")", 1)) {
                            next();
                        } else if (!isOp(")")) {
                            do {
                                bool ignored = false;
                                const std::string paramType = cType(ignored);
                                if (paramType.empty()) {
                                    throw ParseError{};
                                }
                                stmt.params.emplace_back(name(), paramType);
                            } while (accept(","));
                        }
                        expect(")");
                        if (accept(";")) {
                            return Stmt{}; // a prototype: not needed in the other languages
                        }
                        stmt.bodies.push_back(cBlock());
                        return stmt;
                    }
                    if (isOp("[")) {
                        throw ParseError{}; // arrays: by hand
                    }
                    stmt.kind = Stmt::Kind::Declare;
                    stmt.type = type;
                    stmt.constant = constant;
                    stmt.name = declared;
                    if (accept("=")) {
                        stmt.value = expression();
                    }
                    expect(";");
                    return stmt;
                }
                at_ = start;
            }
        }
        stmt = simpleStatement();
        expect(";");
        return stmt;
    }

    /// Assignment, increment or call (no ";").
    Stmt simpleStatement() {
        Stmt stmt;
        if ((isOp("++") || isOp("--")) && peek(1).type == Token::Type::Name) {
            const std::string op = next().text;
            stmt.kind = Stmt::Kind::Assign;
            stmt.name = name();
            stmt.op = op == "++" ? "+=" : "-=";
            stmt.value = make(Expr::Kind::Number, "1");
            return stmt;
        }
        if (peek().type == Token::Type::Name) {
            static const std::set<std::string> assignments{"=", "+=", "-=", "*=", "/=", "%="};
            if (peek(1).type == Token::Type::Op && assignments.count(peek(1).text) != 0) {
                stmt.kind = Stmt::Kind::Assign;
                stmt.name = next().text;
                stmt.op = next().text;
                stmt.value = expression();
                return stmt;
            }
            if (isOp("++", 1) || isOp("--", 1)) {
                stmt.kind = Stmt::Kind::Assign;
                stmt.name = next().text;
                stmt.op = next().text == "++" ? "+=" : "-=";
                stmt.value = make(Expr::Kind::Number, "1");
                return stmt;
            }
        }
        stmt.kind = Stmt::Kind::Expression;
        stmt.value = expression();
        if (stmt.value->kind != Expr::Kind::Call) {
            throw ParseError{}; // "a + b;" does nothing: by hand
        }
        return stmt;
    }

    Stmt cFor() {
        next(); // for
        expect("(");
        Stmt init;
        bool hasInit = false;
        if (!isOp(";")) {
            bool constant = false;
            if (cpp_) {
                (void)cType(constant);
            } else if (isName("let") || isName("var") || isName("const")) {
                next();
            }
            init.kind = Stmt::Kind::Assign;
            init.name = name();
            init.op = "=";
            if (!cpp_ && isName("of")) { // for (const x of [...])
                next();
                Stmt each;
                each.kind = Stmt::Kind::ForEach;
                each.name = init.name;
                each.value = expression();
                expect(")");
                each.bodies.push_back(cBlock());
                return each;
            }
            expect("=");
            init.value = expression();
            hasInit = true;
        }
        expect(";");
        const ExprPtr condition = isOp(";") ? make(Expr::Kind::Bool, "true") : expression();
        expect(";");
        Stmt update;
        bool hasUpdate = false;
        if (!isOp(")")) {
            update = simpleStatement();
            hasUpdate = true;
        }
        expect(")");
        Body body = cBlock();

        // Counting loop: for (i = a; i < b; i += k)
        if (hasInit && hasUpdate && update.kind == Stmt::Kind::Assign && update.name == init.name &&
            (update.op == "+=" || update.op == "-=") && condition->kind == Expr::Kind::Binary &&
            condition->items[0]->kind == Expr::Kind::Name && condition->items[0]->text == init.name) {
            const std::string& op = condition->text;
            const bool up = update.op == "+=";
            if ((up && (op == "<" || op == "<=")) || (!up && (op == ">" || op == ">="))) {
                Stmt loop;
                loop.kind = Stmt::Kind::ForRange;
                loop.name = init.name;
                loop.start = init.value;
                ExprPtr end = condition->items[1];
                if (op == "<=") {
                    end = make(Expr::Kind::Binary, "+", {end, make(Expr::Kind::Number, "1")});
                } else if (op == ">=") {
                    end = make(Expr::Kind::Binary, "-", {end, make(Expr::Kind::Number, "1")});
                }
                loop.end = end;
                loop.step = up ? update.value : make(Expr::Kind::Unary, "-", {update.value});
                loop.bodies.push_back(std::move(body));
                return loop;
            }
        }
        // Any other for: the start, then a while loop ending with the update.
        Stmt loop;
        loop.kind = Stmt::Kind::While;
        loop.value = condition;
        if (hasUpdate) {
            body.push_back(update);
        }
        loop.bodies.push_back(std::move(body));
        if (!hasInit) {
            return loop;
        }
        Stmt sequence;
        sequence.kind = Stmt::Kind::Sequence;
        sequence.bodies.push_back(Body{init, loop});
        return sequence;
    }

    // --- Python statements ---

    void endLine() {
        if (peek().type == Token::Type::Comment) { // a comment at the end of the line: dropped
            next();
        }
        if (peek().type == Token::Type::Newline) {
            next();
        } else if (peek().type != Token::Type::End && peek().type != Token::Type::Dedent) {
            throw ParseError{};
        }
    }

    Body pythonBlock() {
        expect(":");
        Body body;
        if (peek().type != Token::Type::Newline && peek().type != Token::Type::Comment) {
            statementInto(body, false); // "if x: y" on one line
            return body;
        }
        endLine();
        while (peek().type == Token::Type::Comment) {
            Stmt comment;
            comment.kind = Stmt::Kind::Comment;
            comment.name = next().text;
            comment.line = tokens_[at_ - 1].line;
            body.push_back(comment);
        }
        if (peek().type != Token::Type::Indent) {
            throw ParseError{};
        }
        next();
        while (peek().type != Token::Type::Dedent && peek().type != Token::Type::End) {
            if (peek().type == Token::Type::Newline) {
                next();
                continue;
            }
            statementInto(body, false);
        }
        if (peek().type == Token::Type::Dedent) {
            next();
        }
        return body;
    }

    Stmt pythonStatement(bool topLevel) {
        Stmt stmt;
        const Token& token = peek();
        if (token.type == Token::Type::Comment) {
            stmt.kind = Stmt::Kind::Comment;
            stmt.name = next().text;
            return stmt;
        }
        if (token.type == Token::Type::Name) {
            const std::string word = token.text;
            if (word == "def") {
                next();
                stmt.kind = Stmt::Kind::Function;
                stmt.name = name();
                expect("(");
                if (!isOp(")")) {
                    do {
                        stmt.params.emplace_back(name(), "");
                    } while (accept(","));
                }
                expect(")");
                stmt.bodies.push_back(pythonBlock());
                return stmt;
            }
            if (word == "if") {
                next();
                stmt.kind = Stmt::Kind::If;
                stmt.conditions.push_back(expression());
                stmt.bodies.push_back(pythonBlock());
                while (isName("elif")) {
                    next();
                    stmt.conditions.push_back(expression());
                    stmt.bodies.push_back(pythonBlock());
                }
                if (isName("else")) {
                    next();
                    stmt.bodies.push_back(pythonBlock());
                }
                return stmt;
            }
            if (word == "while") {
                next();
                stmt.kind = Stmt::Kind::While;
                stmt.value = expression();
                stmt.bodies.push_back(pythonBlock());
                return stmt;
            }
            if (word == "for") {
                next();
                stmt.name = name();
                if (!isName("in")) {
                    throw ParseError{};
                }
                next();
                const ExprPtr source = expression();
                if (source->kind == Expr::Kind::Call && source->text == "range" && !source->items.empty() &&
                    source->items.size() <= 3) {
                    stmt.kind = Stmt::Kind::ForRange;
                    const auto& args = source->items;
                    stmt.start = args.size() == 1 ? make(Expr::Kind::Number, "0") : args[0];
                    stmt.end = args.size() == 1 ? args[0] : args[1];
                    stmt.step = args.size() == 3 ? args[2] : make(Expr::Kind::Number, "1");
                } else if (source->kind == Expr::Kind::List) {
                    stmt.kind = Stmt::Kind::ForEach;
                    stmt.value = source;
                } else {
                    throw ParseError{};
                }
                stmt.bodies.push_back(pythonBlock());
                return stmt;
            }
            if (word == "return") {
                next();
                stmt.kind = Stmt::Kind::Return;
                if (peek().type != Token::Type::Newline && peek().type != Token::Type::Comment &&
                    peek().type != Token::Type::End) {
                    stmt.value = expression();
                }
                endLine();
                return stmt;
            }
            if (word == "break" || word == "continue" || word == "pass") {
                next();
                stmt.kind = word == "break"      ? Stmt::Kind::Break
                            : word == "continue" ? Stmt::Kind::Continue
                                                 : Stmt::Kind::Blank;
                endLine();
                return stmt;
            }
            if (word == "global" || word == "import" || word == "from") {
                // global: found again when writing Python; import time / math: their names are common.
                while (peek().type != Token::Type::Newline && peek().type != Token::Type::End) {
                    next();
                }
                endLine();
                return stmt; // Blank
            }
        }
        stmt = simpleStatement();
        if (stmt.kind == Stmt::Kind::Assign && stmt.op == "=" && topLevel) {
            stmt.kind = Stmt::Kind::Declare; // module-level assignment: a global variable
        }
        endLine();
        return stmt;
    }

    std::vector<Token> tokens_;
    std::size_t at_ = 0;
    bool python_;
    bool cpp_;
    std::vector<std::string> lines_;
    int lastLine_ = 0;
};

// --- Writing -------------------------------------------------------------------------------------

/// Binding strength of an expression (higher binds tighter); Python's "not" is weaker than a comparison.
int precedence(const Expr& expr, bool python) {
    switch (expr.kind) {
    case Expr::Kind::Ternary:
        return 1;
    case Expr::Kind::Binary:
        if (expr.text == "or") {
            return 2;
        }
        if (expr.text == "and") {
            return 3;
        }
        if (expr.text == "==" || expr.text == "!=" || expr.text == "<" || expr.text == ">" || expr.text == "<=" ||
            expr.text == ">=") {
            return 5;
        }
        if (expr.text == "+" || expr.text == "-") {
            return 6;
        }
        return 7; // * / % //
    case Expr::Kind::Unary:
        return expr.text == "not" && python ? 4 : 8;
    default:
        return 10;
    }
}

bool isComparison(const Expr& expr) {
    return expr.kind == Expr::Kind::Binary && precedence(expr, false) == 5;
}

class Writer {
public:
    Writer(ProgramLanguage language, const Body& program) : language_(language) {
        for (const Stmt& stmt : program) {
            if (stmt.kind == Stmt::Kind::Declare || (stmt.kind == Stmt::Kind::Assign && stmt.op == "=")) {
                globals_.insert(stmt.name);
                types_[stmt.name] = typeOf(stmt);
            }
        }
        for (const Stmt& stmt : program) {
            if (stmt.kind == Stmt::Kind::Function) {
                functions_[stmt.name] = returnType(stmt);
            }
        }
    }

    std::string write(const Body& program, std::vector<int>& toCheck) {
        toCheck_ = &toCheck;
        std::ostringstream out;
        if (python() && usesMath(program)) {
            out << "import math\n\n";
        }
        Scope scope;
        block(out, program, 0, scope, true);
        std::string text = out.str();
        while (!text.empty() && text.back() == '\n') {
            text.pop_back();
        }
        return text + "\n";
    }

private:
    struct Scope {
        std::set<std::string> declared;
    };

    bool python() const { return language_ == ProgramLanguage::Python; }
    bool cpp() const { return language_ == ProgramLanguage::Cpp; }

    static std::string indent(int depth) { return std::string(static_cast<std::size_t>(depth) * 4, ' '); }

    std::string comment(const std::string& text) const { return (python() ? "# " : "// ") + text; }

    static bool usesMathIn(const ExprPtr& expr) {
        if (!expr) {
            return false;
        }
        if (expr->kind == Expr::Kind::Call && (expr->text == "floor" || expr->text == "sqrt")) {
            return true;
        }
        if (expr->kind == Expr::Kind::Binary && expr->text == "//") {
            return false;
        }
        return std::any_of(expr->items.begin(), expr->items.end(), usesMathIn);
    }

    static bool usesMath(const Body& body) {
        return std::any_of(body.begin(), body.end(), [](const Stmt& stmt) {
            return usesMathIn(stmt.value) || usesMathIn(stmt.start) || usesMathIn(stmt.end) || usesMathIn(stmt.step) ||
                   std::any_of(stmt.conditions.begin(), stmt.conditions.end(), usesMathIn) ||
                   std::any_of(stmt.bodies.begin(), stmt.bodies.end(), [](const Body& inner) { return usesMath(inner); });
        });
    }

    // --- C++ types ---

    std::string typeOfExpr(const ExprPtr& expr) const {
        if (!expr) {
            return "float";
        }
        switch (expr->kind) {
        case Expr::Kind::Number:
            return expr->text.rfind("0x", 0) != 0 && expr->text.find_first_of(".eE") != std::string::npos ? "float" : "int";
        case Expr::Kind::String:
            return "String";
        case Expr::Kind::Bool:
            return "bool";
        case Expr::Kind::Name: {
            if (const auto found = types_.find(expr->text); found != types_.end()) {
                return found->second;
            }
            static const std::set<std::string> constants{"HIGH", "LOW", "INPUT", "OUTPUT", "INPUT_PULLUP", "LED_BUILTIN"};
            const bool analogPin = expr->text.size() >= 2 && expr->text[0] == 'A' && digit(expr->text[1]);
            return constants.count(expr->text) != 0 || analogPin ? "int" : "float";
        }
        case Expr::Kind::Unary:
            return expr->text == "not" ? "bool" : typeOfExpr(expr->items[0]);
        case Expr::Kind::Binary: {
            if (expr->text == "and" || expr->text == "or" || isComparison(*expr)) {
                return "bool";
            }
            const std::string left = typeOfExpr(expr->items[0]);
            const std::string right = typeOfExpr(expr->items[1]);
            if (left == "String" || right == "String") {
                return "String";
            }
            if (left == "float" || right == "float") {
                return "float";
            }
            return left == "unsigned long" || right == "unsigned long" ? "unsigned long" : "int";
        }
        case Expr::Kind::Ternary:
            return typeOfExpr(expr->items[1]);
        case Expr::Kind::Call: {
            static const std::map<std::string, std::string> known{
                {"millis", "unsigned long"}, {"micros", "unsigned long"}, {"digitalRead", "int"}, {"analogRead", "int"},
                {"int", "int"},              {"floor", "int"},            {"round", "int"},       {"random", "long"}};
            if (const auto found = known.find(expr->text); found != known.end()) {
                return found->second;
            }
            if (const auto found = functions_.find(expr->text); found != functions_.end() && found->second != "void") {
                return found->second;
            }
            return "float";
        }
        case Expr::Kind::List:
            return expr->items.empty() ? "float" : typeOfExpr(expr->items[0]);
        }
        return "float";
    }

    std::string typeOf(const Stmt& stmt) const {
        if (!stmt.type.empty()) {
            return stmt.type == "double" ? "float" : stmt.type == "boolean" ? "bool" : stmt.type;
        }
        return typeOfExpr(stmt.value);
    }

    /// Its C++ type; else void without a returned value; else the type of the first returned value.
    std::string returnType(const Stmt& function) const {
        if (!function.type.empty()) {
            return function.type == "double" ? "float" : function.type;
        }
        const ExprPtr returned = firstReturn(function.bodies[0]);
        if (!returned) {
            return "void";
        }
        const std::string type = typeOfExpr(returned);
        return type == "bool" || type == "String" || type == "int" ? type : "float";
    }

    static ExprPtr firstReturn(const Body& body) {
        for (const Stmt& stmt : body) {
            if (stmt.kind == Stmt::Kind::Return && stmt.value) {
                return stmt.value;
            }
            for (const Body& inner : stmt.bodies) {
                if (ExprPtr found = firstReturn(inner)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    /// Names used as a count (a bound of a counting loop) or as a pin: int, not float.
    static void intUses(const Body& body, std::set<std::string>& names) {
        static const std::set<std::string> pinFunctions{"pinMode", "digitalWrite", "digitalRead", "analogRead",
                                                        "analogWrite", "tone", "noTone", "servoWrite"};
        const auto scan = [&](const ExprPtr& expr, const auto& self) -> void {
            if (!expr) {
                return;
            }
            if (expr->kind == Expr::Kind::Call && pinFunctions.count(expr->text) != 0 && !expr->items.empty() &&
                expr->items[0]->kind == Expr::Kind::Name) {
                names.insert(expr->items[0]->text);
            }
            for (const ExprPtr& item : expr->items) {
                self(item, self);
            }
        };
        for (const Stmt& stmt : body) {
            if (stmt.kind == Stmt::Kind::ForRange) {
                const auto boundNames = [&](const ExprPtr& expr, const auto& self) -> void {
                    if (!expr || expr->kind == Expr::Kind::Call) {
                        return;
                    }
                    if (expr->kind == Expr::Kind::Name) {
                        names.insert(expr->text);
                    }
                    for (const ExprPtr& item : expr->items) {
                        self(item, self);
                    }
                };
                boundNames(stmt.start, boundNames);
                boundNames(stmt.end, boundNames);
            }
            scan(stmt.value, scan);
            for (const ExprPtr& condition : stmt.conditions) {
                scan(condition, scan);
            }
            for (const Body& inner : stmt.bodies) {
                intUses(inner, names);
            }
        }
    }

    // --- Expressions ---

    static std::string quote(const std::string& text) {
        std::string out = "\"";
        for (const char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\t') {
                out += "\\t";
            } else {
                out += c;
            }
        }
        return out + "\"";
    }

    std::string sub(const ExprPtr& expr, int minimum) const {
        const std::string text = expression(expr);
        return precedence(*expr, python()) < minimum ? "(" + text + ")" : text;
    }

    std::string args(const std::vector<ExprPtr>& items) const {
        std::string out;
        for (const ExprPtr& item : items) {
            out += (out.empty() ? "" : ", ") + expression(item);
        }
        return out;
    }

    std::string callName(const std::string& name) const {
        if (name == "print") {
            return cpp() ? "Serial.println" : "print";
        }
        static const std::set<std::string> maths{"abs", "min", "max", "floor", "round", "sqrt", "pow"};
        if (language_ == ProgramLanguage::JavaScript && maths.count(name) != 0) {
            return "Math." + name;
        }
        if (python() && (name == "floor" || name == "sqrt")) {
            return "math." + name;
        }
        return name;
    }

    std::string expression(const ExprPtr& expr) const {
        switch (expr->kind) {
        case Expr::Kind::Number:
        case Expr::Kind::Name:
            return expr->text;
        case Expr::Kind::String:
            return quote(expr->text);
        case Expr::Kind::Bool:
            return python() ? (expr->text == "true" ? "True" : "False") : expr->text;
        case Expr::Kind::Unary:
            if (expr->text == "not") {
                return python() ? "not " + sub(expr->items[0], 4) : "!" + sub(expr->items[0], 8);
            }
            return "-" + sub(expr->items[0], 8);
        case Expr::Kind::Binary: {
            const int level = precedence(*expr, python());
            std::string op = expr->text;
            if (op == "//" && !python()) {
                return (cpp() ? "floor(" : "Math.floor(") + sub(expr->items[0], 7) + " / " + sub(expr->items[1], 8) + ")";
            }
            if (!python()) {
                op = op == "and" ? "&&" : op == "or" ? "||" : op;
            }
            // Left-associative: the right operand needs parentheses at the same level ("a - (b - c)").
            return sub(expr->items[0], level) + " " + op + " " + sub(expr->items[1], level + 1);
        }
        case Expr::Kind::Ternary:
            if (python()) {
                return sub(expr->items[1], 2) + " if " + sub(expr->items[0], 2) + " else " + sub(expr->items[2], 1);
            }
            return sub(expr->items[0], 2) + " ? " + sub(expr->items[1], 2) + " : " + sub(expr->items[2], 1);
        case Expr::Kind::Call:
            if ((expr->text == "int" || expr->text == "float") && expr->items.size() == 1) {
                const std::string inner = expression(expr->items[0]);
                if (python()) {
                    return expr->text + "(" + inner + ")";
                }
                if (cpp()) {
                    return "(" + expr->text + ")(" + inner + ")";
                }
                return (expr->text == "int" ? "Math.trunc(" : "Number(") + inner + ")";
            }
            if (expr->text == "pow" && expr->items.size() == 2 && !cpp()) {
                return sub(expr->items[0], 9) + " ** " + sub(expr->items[1], 8);
            }
            return callName(expr->text) + "(" + args(expr->items) + ")";
        case Expr::Kind::List:
            if (cpp()) {
                return "{" + args(expr->items) + "}";
            }
            if (python()) {
                return "(" + args(expr->items) + (expr->items.size() == 1 ? ",)" : ")");
            }
            return "[" + args(expr->items) + "]";
        }
        return {};
    }

    // --- Statements ---

    static void line(std::ostringstream& out, int depth, const std::string& text) {
        out << indent(depth) << text << '\n';
    }

    /// The names a body assigns (Python: the globals among them need "global").
    static void assigned(const Body& body, std::set<std::string>& names) {
        for (const Stmt& stmt : body) {
            if (stmt.kind == Stmt::Kind::Assign || stmt.kind == Stmt::Kind::Declare ||
                stmt.kind == Stmt::Kind::ForRange || stmt.kind == Stmt::Kind::ForEach) {
                names.insert(stmt.name);
            }
            for (const Body& inner : stmt.bodies) {
                assigned(inner, names);
            }
        }
    }

    void block(std::ostringstream& out, const Body& body, int depth, Scope& scope, bool topLevel) {
        bool wrote = false;
        for (const Stmt& stmt : body) {
            wrote = statement(out, stmt, depth, scope, topLevel) || wrote;
        }
        if (!wrote && python() && !topLevel) {
            line(out, depth, "pass");
        }
    }

    void openBlock(std::ostringstream& out, int depth, const std::string& head) const {
        line(out, depth, head + (python() ? ":" : " {"));
    }

    void closeBlock(std::ostringstream& out, int depth) const {
        if (!python()) {
            line(out, depth, "}");
        }
    }

    void subBlock(std::ostringstream& out, const Body& body, int depth, const Scope& scope,
                  const std::string& loopVariable = {}) {
        Scope inner = scope;
        if (!loopVariable.empty()) {
            inner.declared.insert(loopVariable);
        }
        block(out, body, depth, inner, false);
    }

    std::string declaration(const Stmt& stmt, Scope& scope, const std::string& type, bool constant) const {
        scope.declared.insert(stmt.name);
        std::string value;
        if (stmt.value) {
            value = " = " + expression(stmt.value);
        } else if (!cpp()) {
            value = " = 0";
        }
        if (python()) {
            return stmt.name + value;
        }
        if (cpp()) {
            const bool list = stmt.value && stmt.value->kind == Expr::Kind::List; // an array in C++
            return std::string(constant ? "const " : "") + type + " " + stmt.name + (list ? "[]" : "") + value + ";";
        }
        return std::string(constant ? "const " : "let ") + stmt.name + value + ";";
    }

    static bool isConstantName(const std::string& name) {
        const auto upper = [](char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; };
        const auto lower = [](char c) { return std::islower(static_cast<unsigned char>(c)) != 0; };
        return std::any_of(name.begin(), name.end(), upper) && std::none_of(name.begin(), name.end(), lower);
    }

    /// Writes one statement; false when it wrote nothing that counts as code (comments, blank lines).
    bool statement(std::ostringstream& out, const Stmt& stmt, int depth, Scope& scope, bool topLevel) {
        const std::string end = python() ? "" : ";";
        switch (stmt.kind) {
        case Stmt::Kind::Blank:
            out << '\n';
            return false;
        case Stmt::Kind::Comment:
            if (cpp() && stmt.name.rfind("C++: #", 0) == 0) { // a C++ line kept by an earlier conversion
                line(out, depth, stmt.name.substr(5));
                return false;
            }
            line(out, depth, comment(stmt.name));
            return false;
        case Stmt::Kind::Directive:
            if (cpp()) {
                line(out, depth, stmt.name);
            } else {
                line(out, depth, comment("C++: " + stmt.name));
                toCheck_->push_back(stmt.line);
            }
            return false;
        case Stmt::Kind::Unknown: {
            toCheck_->push_back(stmt.line);
            line(out, depth, comment("A convertir a la main / to convert by hand:"));
            std::istringstream lines(stmt.name);
            std::string text;
            while (std::getline(lines, text)) {
                line(out, depth, comment(trim(text)));
            }
            return false;
        }
        case Stmt::Kind::Declare: {
            const bool constant = stmt.constant || (topLevel && !python() && stmt.value && isConstantName(stmt.name));
            line(out, depth, declaration(stmt, scope, typeOf(stmt), constant));
            return true;
        }
        case Stmt::Kind::Assign:
            if (stmt.op == "=" && !python() && scope.declared.count(stmt.name) == 0 && globals_.count(stmt.name) == 0) {
                line(out, depth, declaration(stmt, scope, typeOfExpr(stmt.value), false));
                return true;
            }
            if (!python() && (stmt.op == "+=" || stmt.op == "-=") && stmt.value->kind == Expr::Kind::Number &&
                stmt.value->text == "1") {
                line(out, depth, stmt.name + (stmt.op == "+=" ? "++" : "--") + end);
                return true;
            }
            line(out, depth, stmt.name + " " + stmt.op + " " + expression(stmt.value) + end);
            return true;
        case Stmt::Kind::Expression:
            if (!cpp() && stmt.value->kind == Expr::Kind::Call && stmt.value->text == "Serial.begin") {
                return false; // the scripts print without it
            }
            line(out, depth, expression(stmt.value) + end);
            return true;
        case Stmt::Kind::Return:
            line(out, depth, std::string("return") + (stmt.value ? " " + expression(stmt.value) : "") + end);
            return true;
        case Stmt::Kind::Break:
            line(out, depth, "break" + end);
            return true;
        case Stmt::Kind::Continue:
            line(out, depth, "continue" + end);
            return true;
        case Stmt::Kind::Sequence:
            block(out, stmt.bodies[0], depth, scope, false);
            return true;
        case Stmt::Kind::If:
            for (std::size_t i = 0; i < stmt.bodies.size(); ++i) {
                if (i == 0) {
                    const std::string condition = expression(stmt.conditions[0]);
                    openBlock(out, depth, python() ? "if " + condition : "if (" + condition + ")");
                } else if (i < stmt.conditions.size()) {
                    const std::string condition = expression(stmt.conditions[i]);
                    line(out, depth, python() ? "elif " + condition + ":" : "} else if (" + condition + ") {");
                } else {
                    line(out, depth, python() ? "else:" : "} else {");
                }
                subBlock(out, stmt.bodies[i], depth + 1, scope);
            }
            closeBlock(out, depth);
            return true;
        case Stmt::Kind::While:
            openBlock(out, depth, python() ? "while " + expression(stmt.value) : "while (" + expression(stmt.value) + ")");
            subBlock(out, stmt.bodies[0], depth + 1, scope);
            closeBlock(out, depth);
            return true;
        case Stmt::Kind::ForRange:
            openBlock(out, depth, forRangeHead(stmt, scope));
            subBlock(out, stmt.bodies[0], depth + 1, scope, stmt.name);
            closeBlock(out, depth);
            return true;
        case Stmt::Kind::ForEach: {
            std::string head;
            if (python()) {
                head = "for " + stmt.name + " in " + expression(stmt.value);
            } else if (cpp()) {
                // The values in an array first: C++ has no list literal to loop over.
                const std::string type = typeOfExpr(stmt.value);
                const std::string values = stmt.name + "Values";
                line(out, depth, "const " + type + " " + values + "[] = " + expression(stmt.value) + ";");
                head = "for (" + type + " " + stmt.name + " : " + values + ")";
            } else {
                head = "for (const " + stmt.name + " of " + expression(stmt.value) + ")";
            }
            openBlock(out, depth, head);
            subBlock(out, stmt.bodies[0], depth + 1, scope, stmt.name);
            closeBlock(out, depth);
            return true;
        }
        case Stmt::Kind::Function:
            function(out, stmt, depth);
            return true;
        }
        return false;
    }

    std::string forRangeHead(const Stmt& stmt, const Scope& scope) const {
        const bool unitStep = stmt.step->kind == Expr::Kind::Number && stmt.step->text == "1";
        if (python()) {
            const bool zeroStart = stmt.start->kind == Expr::Kind::Number && stmt.start->text == "0";
            std::string range = zeroStart && unitStep ? expression(stmt.end)
                                                      : expression(stmt.start) + ", " + expression(stmt.end);
            if (!unitStep) {
                range += ", " + expression(stmt.step);
            }
            return "for " + stmt.name + " in range(" + range + ")";
        }
        const bool down = stmt.step->kind == Expr::Kind::Unary && stmt.step->text == "-";
        const ExprPtr amount = down ? stmt.step->items[0] : stmt.step;
        const bool one = amount->kind == Expr::Kind::Number && amount->text == "1";
        const std::string update =
            one ? stmt.name + (down ? "--" : "++") : stmt.name + (down ? " -= " : " += ") + expression(amount);
        const bool known = scope.declared.count(stmt.name) != 0 || globals_.count(stmt.name) != 0;
        const std::string declared = known ? "" : cpp() ? "int " : "let ";
        return "for (" + declared + stmt.name + " = " + expression(stmt.start) + "; " + stmt.name + (down ? " > " : " < ") +
               expression(stmt.end) + "; " + update + ")";
    }

    void function(std::ostringstream& out, const Stmt& stmt, int depth) {
        Scope scope;
        std::set<std::string> ints;
        intUses(stmt.bodies[0], ints);
        const auto savedTypes = types_; // parameters hide the globals of the same name
        std::string params;
        for (const auto& [name, type] : stmt.params) {
            scope.declared.insert(name);
            const std::string chosen = !type.empty() ? type : ints.count(name) != 0 ? "int" : "float";
            types_[name] = chosen;
            params += (params.empty() ? "" : ", ") + (cpp() ? chosen + " " + name : name);
        }
        if (python()) {
            openBlock(out, depth, "def " + stmt.name + "(" + params + ")");
            std::set<std::string> names;
            assigned(stmt.bodies[0], names);
            std::string shared;
            for (const std::string& name : names) {
                if (globals_.count(name) != 0 && scope.declared.count(name) == 0) {
                    shared += (shared.empty() ? "" : ", ") + name;
                }
            }
            if (!shared.empty()) {
                line(out, depth + 1, "global " + shared);
            }
        } else if (cpp()) {
            openBlock(out, depth, returnType(stmt) + " " + stmt.name + "(" + params + ")");
        } else {
            openBlock(out, depth, "function " + stmt.name + "(" + params + ")");
        }
        block(out, stmt.bodies[0], depth + 1, scope, false);
        closeBlock(out, depth);
        types_ = savedTypes;
    }

    ProgramLanguage language_;
    std::set<std::string> globals_;
    std::map<std::string, std::string> types_;     ///< Globals (and parameters inside a function): C++ type.
    std::map<std::string, std::string> functions_; ///< Function: C++ return type.
    std::vector<int>* toCheck_ = nullptr;
};

} // namespace

Converted convert(std::string_view source, ProgramLanguage from, ProgramLanguage to) {
    if (from == ProgramLanguage::Blocks || to == ProgramLanguage::Blocks) {
        return {std::string(source), {}};
    }
    Parser parser(from == ProgramLanguage::Python ? lexPython(source) : lexC(source), from, source);
    const Body program = parser.program();
    Converted converted;
    Writer writer(to, program);
    converted.code = writer.write(program, converted.toCheck);
    std::sort(converted.toCheck.begin(), converted.toCheck.end());
    converted.toCheck.erase(std::unique(converted.toCheck.begin(), converted.toCheck.end()), converted.toCheck.end());
    return converted;
}

} // namespace s2a::convert
