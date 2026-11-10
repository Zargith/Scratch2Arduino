#include "s2a/usercode.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

namespace s2a::usercode {

namespace {

using project::ProgramLanguage;

std::string trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

/// Whitespace runs (newlines included) become one space.
std::string squeeze(std::string_view text) {
    std::string result;
    bool space = false;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            space = true;
            continue;
        }
        if (space && !result.empty()) {
            result += ' ';
        }
        space = false;
        result += c;
    }
    return result;
}

/// Splits on top-level commas (not inside brackets).
std::vector<std::string> splitCommas(std::string_view text) {
    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (const char c : text) {
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        }
        if (c == ',' && depth == 0) {
            parts.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!trim(current).empty() || !parts.empty()) {
        parts.push_back(trim(current));
    }
    return parts;
}

/// C-like code (C++, JavaScript) with comments, strings and (C++) preprocessor lines blanked
/// out by spaces: line numbers and positions are kept.
std::string blankComments(std::string_view code, bool preprocessor, bool templates) {
    std::string out(code);
    std::size_t i = 0;
    bool lineStart = true;
    while (i < out.size()) {
        const char c = out[i];
        if (c == '\n') {
            lineStart = true;
            ++i;
            continue;
        }
        if (preprocessor && lineStart && c == '#') {
            while (i < out.size() && out[i] != '\n') {
                // A backslash at the end of a line continues the directive.
                if (out[i] == '\\' && i + 1 < out.size() && out[i + 1] == '\n') {
                    out[i] = ' ';
                    i += 2;
                    continue;
                }
                out[i++] = ' ';
            }
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            lineStart = false;
        }
        if (c == '/' && i + 1 < out.size() && out[i + 1] == '/') {
            while (i < out.size() && out[i] != '\n') {
                out[i++] = ' ';
            }
            continue;
        }
        if (c == '/' && i + 1 < out.size() && out[i + 1] == '*') {
            out[i] = out[i + 1] = ' ';
            i += 2;
            while (i < out.size() && !(out[i] == '*' && i + 1 < out.size() && out[i + 1] == '/')) {
                if (out[i] != '\n') {
                    out[i] = ' ';
                }
                ++i;
            }
            if (i < out.size()) {
                out[i] = ' ';
                if (i + 1 < out.size()) {
                    out[i + 1] = ' ';
                }
                i += 2;
            }
            continue;
        }
        if (c == '"' || c == '\'' || (templates && c == '`')) {
            const char quote = c;
            ++i; // the quotes stay: "x = 'a'" is still an assignment
            while (i < out.size() && out[i] != quote) {
                if (out[i] == '\\' && i + 1 < out.size()) {
                    out[i++] = ' ';
                    if (out[i] != '\n') {
                        out[i] = ' ';
                    }
                    ++i;
                    continue;
                }
                if (out[i] == '\n') {
                    if (quote != '`') {
                        break; // unterminated string: it ends with its line
                    }
                    ++i;
                    continue;
                }
                out[i++] = ' ';
            }
            if (i < out.size() && out[i] == quote) {
                ++i;
            }
            continue;
        }
        ++i;
    }
    return out;
}

struct Statement {
    std::string text; ///< Squeezed.
    int line = 0;
    bool block = false;  ///< Ended by '{' (its body follows).
    std::string body;    ///< Text inside the braces, for blocks.
};

/// Top-level statements of C-like code: ended by ';' or by a '{ ... }' body. With `newlines`,
/// a line break at the top level also ends a statement (JavaScript without semicolons).
std::vector<Statement> topLevel(const std::string& code, bool newlines) {
    std::vector<Statement> statements;
    std::string current;
    int line = 1;
    int startLine = 1;
    int parens = 0;
    std::size_t i = 0;
    const auto finish = [&](bool block, std::string body) {
        Statement statement{squeeze(current), startLine, block, std::move(body)};
        if (!statement.text.empty() || block) {
            statements.push_back(std::move(statement));
        }
        current.clear();
    };
    while (i < code.size()) {
        const char c = code[i];
        if (current.find_first_not_of(" \t\r\n") == std::string::npos) {
            startLine = line;
        }
        if (c == '\n') {
            ++line;
            if (newlines && parens == 0) {
                // "let x = 1\nlet y" ends here, "function f(a,\n b)" does not (parens).
                finish(false, {});
                ++i;
                continue;
            }
        }
        if (c == '(') {
            ++parens;
        } else if (c == ')') {
            parens = std::max(0, parens - 1);
        }
        if (c == ';' && parens == 0) {
            finish(false, {});
            ++i;
            continue;
        }
        if (c == '{' && parens == 0) {
            const auto last = current.find_last_not_of(" \t\r\n");
            const bool value = last != std::string::npos && std::string_view("=,:([").find(current[last]) != std::string_view::npos;
            int depth = 1;
            std::size_t j = i + 1;
            while (j < code.size() && depth > 0) {
                if (code[j] == '{') {
                    ++depth;
                } else if (code[j] == '}') {
                    --depth;
                } else if (code[j] == '\n') {
                    ++line;
                }
                ++j;
            }
            if (value) { // an object or initializer list: part of the statement ("b = { x: 2 }")
                current += code.substr(i, j - i);
            } else {
                finish(true, code.substr(i + 1, j >= i + 2 ? j - i - 2 : 0));
            }
            i = j;
            continue;
        }
        current += c;
        ++i;
    }
    finish(false, {});
    return statements;
}

void add(std::vector<Declaration>& found, std::set<std::string>& names, Declaration declaration) {
    if (declaration.name.empty() || declaration.name.front() == '_' || !names.insert(declaration.name).second) {
        return;
    }
    found.push_back(std::move(declaration));
}

// --- C++ ---------------------------------------------------------------------------------

const char* const kCppNumber =
    R"((?:unsigned\s+long|unsigned\s+int|unsigned\s+char|unsigned|long\s+long|long|int|short|float|double|bool|boolean|byte|char|word|u?int(?:8|16|32|64)_t|size_t))";

std::vector<Declaration> cppDeclarations(std::string_view code) {
    static const std::regex function(std::string(R"(^(?:(?:static|inline)\s+)*(void|)") + kCppNumber +
                                     R"()\s+([A-Za-z_]\w*)\s*\(([^()]*)\)$)");
    static const std::regex parameter(std::string(R"(^(?:const\s+)?)") + kCppNumber +
                                      R"(\s+([A-Za-z_]\w*)(?:\s*=.*)?$)");
    static const std::regex variable(std::string(R"(^((?:(?:static|volatile|const)\s+)*))") + kCppNumber +
                                     R"(\s+(.+)$)");
    static const std::regex name(R"(^([A-Za-z_]\w*)\s*(?:=.*)?$)");
    std::vector<Declaration> found;
    std::set<std::string> names{"setup", "loop"};
    for (const Statement& statement : topLevel(blankComments(code, true, false), false)) {
        std::smatch match;
        if (statement.block) {
            if (!std::regex_match(statement.text, match, function)) {
                continue;
            }
            Declaration declaration;
            declaration.kind = Declaration::Kind::Function;
            declaration.name = match[2].str();
            declaration.returnsValue = match[1].str() != "void";
            declaration.line = statement.line;
            bool usable = true;
            const std::string list = trim(match[3].str());
            if (list != "" && list != "void") {
                for (const std::string& part : splitCommas(list)) {
                    std::smatch argument;
                    if (!std::regex_match(part, argument, parameter)) {
                        usable = false;
                        break;
                    }
                    declaration.parameters.push_back(argument[1].str());
                }
            }
            if (usable && declaration.parameters.size() <= kMaxParameters) {
                add(found, names, std::move(declaration));
            }
            continue;
        }
        if (!std::regex_match(statement.text, match, variable)) {
            continue;
        }
        const bool constant = match[1].str().find("const") != std::string::npos;
        for (const std::string& part : splitCommas(match[2].str())) {
            std::smatch each;
            if (std::regex_match(part, each, name)) {
                Declaration declaration;
                declaration.kind = Declaration::Kind::Variable;
                declaration.name = each[1].str();
                declaration.readOnly = constant;
                declaration.line = statement.line;
                add(found, names, std::move(declaration));
            }
        }
    }
    return found;
}

// --- JavaScript --------------------------------------------------------------------------

std::vector<Declaration> javascriptDeclarations(std::string_view code) {
    static const std::regex function(R"(^(?:async\s+)?function\s*\*?\s*([A-Za-z_$][\w$]*)\s*\(([^()]*)\)$)");
    static const std::regex variable(R"(^(let|var|const)\s+(.+)$)");
    static const std::regex name(R"(^([A-Za-z_$][\w$]*)\s*(?:=\s*(.*))?$)");
    static const std::regex parameterName(R"(^([A-Za-z_$][\w$]*)(?:\s*=.*)?$)");
    static const std::regex returns(R"(\breturn\s*[^\s;}])");
    std::vector<Declaration> found;
    std::set<std::string> names;
    for (const Statement& statement : topLevel(blankComments(code, false, true), true)) {
        std::smatch match;
        if (statement.block) {
            if (!std::regex_match(statement.text, match, function)) {
                continue;
            }
            Declaration declaration;
            declaration.kind = Declaration::Kind::Function;
            declaration.name = match[1].str();
            declaration.line = statement.line;
            declaration.returnsValue = std::regex_search(statement.body, returns);
            bool usable = true;
            for (const std::string& part : splitCommas(trim(match[2].str()))) {
                std::smatch argument;
                if (!std::regex_match(part, argument, parameterName)) {
                    usable = false; // ...rest or {destructuring}
                    break;
                }
                declaration.parameters.push_back(argument[1].str());
            }
            if (usable && declaration.parameters.size() <= kMaxParameters) {
                add(found, names, std::move(declaration));
            }
            continue;
        }
        if (!std::regex_match(statement.text, match, variable)) {
            continue;
        }
        for (const std::string& part : splitCommas(match[2].str())) {
            std::smatch each;
            if (!std::regex_match(part, each, name)) {
                continue;
            }
            const std::string value = each[2].str();
            if (value.rfind("function", 0) == 0 || value.find("=>") != std::string::npos) {
                continue; // a function in a variable: not a number
            }
            Declaration declaration;
            declaration.kind = Declaration::Kind::Variable;
            declaration.name = each[1].str();
            declaration.readOnly = match[1].str() == "const";
            declaration.line = statement.line;
            add(found, names, std::move(declaration));
        }
    }
    return found;
}

// --- Python ------------------------------------------------------------------------------

std::vector<Declaration> pythonDeclarations(std::string_view code) {
    static const std::regex function(R"(^(?:async\s+)?def\s+([A-Za-z_]\w*)\s*\((.*)\)\s*(?:->\s*[^:]+)?:.*$)");
    static const std::regex assignment(R"(^([A-Za-z_]\w*)\s*(?::[^=]*)?=(?!=)\s*(.*)$)");
    static const std::regex parameterName(R"(^([A-Za-z_]\w*)\s*(?::[^=]*)?(?:=.*)?$)");
    static const std::regex returns(R"(^\s+return\s+\S)");
    std::vector<std::string> lines;
    {
        std::size_t start = 0;
        while (start <= code.size()) {
            std::size_t end = code.find('\n', start);
            if (end == std::string_view::npos) {
                end = code.size();
            }
            std::string line(code.substr(start, end - start));
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            start = end + 1;
        }
    }
    std::vector<Declaration> found;
    std::set<std::string> names;
    bool inString = false; // inside a """ ... """ string
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        // Triple-quoted strings (docstrings) can hold anything.
        std::size_t quotes = 0;
        for (std::size_t at = line.find("\"\"\""); at != std::string::npos; at = line.find("\"\"\"", at + 3)) {
            ++quotes;
        }
        for (std::size_t at = line.find("'''"); at != std::string::npos; at = line.find("'''", at + 3)) {
            ++quotes;
        }
        const bool wasInString = inString;
        if (quotes % 2 == 1) {
            inString = !inString;
        }
        if (wasInString || line.empty() || std::isspace(static_cast<unsigned char>(line[0])) != 0 || line[0] == '#') {
            continue;
        }
        std::smatch match;
        if (std::regex_match(line, match, function)) {
            Declaration declaration;
            declaration.kind = Declaration::Kind::Function;
            declaration.name = match[1].str();
            declaration.line = static_cast<int>(index) + 1;
            bool usable = true;
            for (const std::string& part : splitCommas(trim(match[2].str()))) {
                std::smatch argument;
                if (!std::regex_match(part, argument, parameterName)) {
                    usable = false; // *args, **kwargs
                    break;
                }
                declaration.parameters.push_back(argument[1].str());
            }
            // Its body: the indented (or blank) lines below.
            for (std::size_t body = index + 1; body < lines.size(); ++body) {
                const std::string& inner = lines[body];
                if (!inner.empty() && std::isspace(static_cast<unsigned char>(inner[0])) == 0 && inner[0] != '#') {
                    break;
                }
                if (std::regex_search(inner, returns)) {
                    declaration.returnsValue = true;
                }
            }
            if (usable && declaration.parameters.size() <= kMaxParameters) {
                add(found, names, std::move(declaration));
            }
        } else if (std::regex_match(line, match, assignment)) {
            const std::string value = trim(match[2].str());
            if (value.rfind("lambda", 0) == 0) {
                continue;
            }
            Declaration declaration;
            declaration.kind = Declaration::Kind::Variable;
            declaration.name = match[1].str();
            declaration.line = static_cast<int>(index) + 1;
            add(found, names, std::move(declaration));
        }
    }
    return found;
}

} // namespace

std::vector<Declaration> declarations(ProgramLanguage language, std::string_view code) {
    switch (language) {
    case ProgramLanguage::Cpp:
        return cppDeclarations(code);
    case ProgramLanguage::Python:
        return pythonDeclarations(code);
    case ProgramLanguage::JavaScript:
        return javascriptDeclarations(code);
    case ProgramLanguage::Blocks:
        break;
    }
    return {};
}

std::vector<Declaration> declarations(const project::Project& project) {
    if (!project.userCode) {
        return {};
    }
    return declarations(project.userCode->language, project.userCode->code);
}

const Declaration* find(const std::vector<Declaration>& declarations, std::string_view name, Declaration::Kind kind) {
    const auto it = std::find_if(declarations.begin(), declarations.end(), [&](const Declaration& declaration) {
        return declaration.kind == kind && declaration.name == name;
    });
    return it != declarations.end() ? &*it : nullptr;
}

bool isUserCodeBlock(std::string_view type) noexcept {
    return type == kCallBlock || type == kValueBlock || type == kGetBlock || type == kSetBlock;
}

bool usesUserCode(const project::Project& project) {
    bool used = false;
    project::forEachBlock(project, [&used](const project::Block& block) { used = used || isUserCodeBlock(block.type); });
    return used;
}

} // namespace s2a::usercode
