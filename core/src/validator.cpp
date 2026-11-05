#include "s2a/validator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <sstream>

namespace s2a::validation {

namespace {

using project::Block;
using project::Stack;

bool isNumber(const std::string& text) {
    std::string trimmed;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            trimmed += c == ',' ? '.' : c;
        }
    }
    if (trimmed.empty()) {
        return false;
    }
    std::istringstream stream(trimmed);
    stream.imbue(std::locale::classic());
    double value = 0;
    stream >> value;
    return !stream.fail() && stream.eof() && std::isfinite(value);
}

class Checker {
public:
    Checker(const project::Project& project, const BlockCatalog& catalog, const Board* board, Language language)
        : project_(project), catalog_(catalog), board_(board), language_(language) {}

    std::vector<Issue> run() {
        bool hasStart = false;
        for (const project::Script& script : project_.scripts) {
            if (script.blocks.empty()) {
                continue;
            }
            const Block& first = script.blocks.front();
            const BlockDefinition* definition = catalog_.find(first.type);
            if (definition == nullptr || definition->shape != BlockShape::Hat) {
                continue; // loose blocks are like comments: they do not run and are not checked
            }
            hasStart = true;
            if (script.blocks.size() == 1) {
                add(Rule::EmptyScript, Severity::Warning, first, {});
            }
            checkStack(script.blocks, 1);
        }
        if (!hasStart) {
            Issue issue;
            issue.rule = Rule::NoStartBlock;
            issue.severity = Severity::Error;
            issue.params = {{"block", name(catalog_.find("events_when_program_starts"))}};
            issues_.push_back(std::move(issue));
        }
        // Errors first, then in the order of the program.
        std::stable_sort(issues_.begin(), issues_.end(),
                         [](const Issue& a, const Issue& b) { return a.severity == Severity::Error && b.severity != Severity::Error; });
        return std::move(issues_);
    }

private:
    /// Block text without its slots: "repeat %1 times %2" -> "repeat … times".
    std::string name(const BlockDefinition* definition) const {
        if (definition == nullptr) {
            return "?";
        }
        const std::string& message = definition->message.get(language_);
        std::string result;
        for (std::size_t i = 0; i < message.size(); ++i) {
            if (message[i] == '%' && i + 1 < message.size() && std::isdigit(static_cast<unsigned char>(message[i + 1])) != 0) {
                while (i + 1 < message.size() && std::isdigit(static_cast<unsigned char>(message[i + 1])) != 0) {
                    ++i;
                }
                result += "…";
            } else {
                result += message[i];
            }
        }
        // Drop a trailing "…" (C-block mouth) and spaces.
        while (!result.empty() && (result.back() == ' ')) {
            result.pop_back();
        }
        const std::string ellipsis = "…";
        if (result.size() >= ellipsis.size() && result.compare(result.size() - ellipsis.size(), ellipsis.size(), ellipsis) == 0) {
            result.erase(result.size() - ellipsis.size());
            while (!result.empty() && result.back() == ' ') {
                result.pop_back();
            }
        }
        return result;
    }

    void add(Rule rule, Severity severity, const Block& block, TranslationParams params) {
        Issue issue;
        issue.rule = rule;
        issue.severity = severity;
        issue.blockId = block.id;
        params.emplace_back("block", name(catalog_.find(block.type)));
        issue.params = std::move(params);
        issues_.push_back(std::move(issue));
    }

    void checkStack(const Stack& stack, std::size_t from) {
        bool afterTerminal = false;
        for (std::size_t i = from; i < stack.size(); ++i) {
            const Block& block = stack[i];
            if (afterTerminal) {
                add(Rule::BlocksAfterForever, Severity::Warning, block, {});
                return; // they never run: no need to check them
            }
            checkBlock(block);
            const BlockDefinition* definition = catalog_.find(block.type);
            afterTerminal = definition != nullptr && definition->terminal;
        }
    }

    std::string capabilityName(PinCapability capability) const {
        switch (capability) {
        case PinCapability::Digital:
            return tr("validation.capability_digital", language_);
        case PinCapability::Analog:
            return tr("validation.capability_analog", language_);
        case PinCapability::Pwm:
            return tr("validation.capability_pwm", language_);
        case PinCapability::Interrupt:
            return tr("validation.capability_interrupt", language_);
        }
        return {};
    }

    void checkPin(const Block& block, const BlockArg& arg) {
        if (board_ == nullptr) {
            return; // unknown board: reported by the build
        }
        const auto found = block.fields.find(arg.name);
        const std::string label = found != block.fields.end() && !found->second.empty() ? found->second : arg.defaultValue;
        const Pin* pin = board_->findPin(label);
        if (pin == nullptr) {
            add(Rule::InvalidPin, Severity::Error, block, {{"pin", label}, {"board", board_->name}});
        } else if (arg.pinCapability && !pin->has(*arg.pinCapability)) {
            std::string usable;
            for (const Pin* candidate : board_->pinsWith(*arg.pinCapability)) {
                usable += (usable.empty() ? "" : ", ") + candidate->label;
            }
            add(Rule::PinCapability, Severity::Error, block,
                {{"pin", label}, {"board", board_->name}, {"capability", capabilityName(*arg.pinCapability)}, {"pins", usable}});
        }
    }

    void checkBlock(const Block& block) {
        const BlockDefinition* definition = catalog_.find(block.type);
        if (definition == nullptr) {
            Issue issue;
            issue.rule = Rule::UnknownBlock;
            issue.blockId = block.id;
            issue.params = {{"block", block.type}};
            issues_.push_back(std::move(issue));
            return;
        }
        for (const BlockArg& arg : definition->args) {
            switch (arg.kind) {
            case ArgKind::Number: {
                const auto input = block.inputs.find(arg.name);
                if (input == block.inputs.end()) {
                    break; // the default value is used
                }
                if (input->second.block) {
                    checkBlock(*input->second.block);
                } else if (!isNumber(input->second.value)) {
                    add(Rule::InvalidNumber, Severity::Error, block, {{"value", input->second.value}});
                }
                break;
            }
            case ArgKind::Text: {
                const auto input = block.inputs.find(arg.name);
                if (input != block.inputs.end() && input->second.block) {
                    checkBlock(*input->second.block);
                }
                break;
            }
            case ArgKind::Boolean: {
                const auto input = block.inputs.find(arg.name);
                if (input == block.inputs.end() || !input->second.block) {
                    add(Rule::MissingCondition, Severity::Error, block, {});
                } else {
                    checkBlock(*input->second.block);
                }
                break;
            }
            case ArgKind::Pin:
                checkPin(block, arg);
                break;
            case ArgKind::Variable: {
                const auto field = block.fields.find(arg.name);
                const bool known = field != block.fields.end() &&
                                   std::any_of(project_.variables.begin(), project_.variables.end(),
                                               [&field](const project::Variable& variable) { return variable.id == field->second; });
                if (!known) {
                    add(Rule::MissingVariable, Severity::Error, block, {});
                }
                break;
            }
            case ArgKind::Statement: {
                const auto stack = block.statements.find(arg.name);
                if (stack == block.statements.end() || stack->second.empty()) {
                    add(Rule::EmptyMouth, Severity::Warning, block, {});
                } else {
                    checkStack(stack->second, 0);
                }
                break;
            }
            case ArgKind::Dropdown:
                break;
            }
        }
    }

    const project::Project& project_;
    const BlockCatalog& catalog_;
    const Board* board_;
    Language language_;
    std::vector<Issue> issues_;
};

} // namespace

std::vector<Issue> validate(const project::Project& project, const BlockCatalog& catalog, const Board* board,
                            Language language) {
    return Checker(project, catalog, board, language).run();
}

std::vector<Issue> validate(const project::Project& project, Language language) {
    return validate(project, BlockCatalog::builtin(), BoardDatabase::builtin().find(project.boardId), language);
}

bool hasErrors(const std::vector<Issue>& issues) {
    return std::any_of(issues.begin(), issues.end(), [](const Issue& issue) { return issue.severity == Severity::Error; });
}

std::string_view ruleKey(Rule rule) noexcept {
    switch (rule) {
    case Rule::NoStartBlock:
        return "validation.no_start_block";
    case Rule::EmptyScript:
        return "validation.empty_script";
    case Rule::EmptyMouth:
        return "validation.empty_mouth";
    case Rule::BlocksAfterForever:
        return "validation.blocks_after_forever";
    case Rule::MissingCondition:
        return "validation.missing_condition";
    case Rule::InvalidNumber:
        return "validation.invalid_number";
    case Rule::InvalidPin:
        return "validation.invalid_pin";
    case Rule::PinCapability:
        return "validation.pin_capability";
    case Rule::MissingVariable:
        return "validation.missing_variable";
    case Rule::UnknownBlock:
        return "validation.unknown_block";
    }
    return "validation.unknown_block";
}

std::string describe(const Issue& issue, Language language) {
    const std::string key(ruleKey(issue.rule));
    return tr(key, language, issue.params) + " " + tr(key + "_hint", language, issue.params);
}

} // namespace s2a::validation
