#include "sheet/FormulaParser.h"

#include <cctype>
#include <stdexcept>
#include <utility>
#include <vector>

#include "common/Exceptions.h"

namespace {

// Recursive descent over the grammar:
//   formula    := '=' comparison
//   comparison := additive [('=' | '<>' | '<' | '<=' | '>' | '>=') additive]
//   additive   := term {('+' | '-') term}
//   term       := unary {('*' | '/') unary}
//   unary      := ('-' | '+') unary | primary
//   primary    := number | "text" | ref | name '(' args ')' | '(' comparison ')'
class Parser {
public:
    explicit Parser(const std::string& source) : src_(source) {}

    FormulaPtr parseFormula() {
        if (!match('=')) {
            fail("a formula must start with '='");
        }
        FormulaPtr formula = parseComparison();
        skipSpace();
        if (pos_ != src_.size()) {
            fail(std::string("unexpected '") + src_[pos_] + "'");
        }
        return formula;
    }

private:
    const std::string& src_;
    std::size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& message) const {
        throw FormulaSyntaxException("\"" + src_ + "\" is not a valid formula: " + message);
    }

    void skipSpace() {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    bool atAlpha() const { return pos_ < src_.size() && std::isalpha(static_cast<unsigned char>(src_[pos_])); }
    bool atDigit() const { return pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_])); }

    bool match(char c) {
        skipSpace();
        if (pos_ < src_.size() && src_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!match(c)) {
            fail(std::string("expected '") + c + "'");
        }
    }

    bool matchComparison(BinaryFormula::Op& op) {
        using Op = BinaryFormula::Op;
        skipSpace();
        if (pos_ >= src_.size()) {
            return false;
        }
        const char c = src_[pos_];
        const char next = pos_ + 1 < src_.size() ? src_[pos_ + 1] : '\0';
        std::size_t length = 1;
        if (c == '<' && next == '=') { op = Op::LessEqual; length = 2; }
        else if (c == '<' && next == '>') { op = Op::NotEqual; length = 2; }
        else if (c == '>' && next == '=') { op = Op::GreaterEqual; length = 2; }
        else if (c == '<') { op = Op::Less; }
        else if (c == '>') { op = Op::Greater; }
        else if (c == '=') { op = Op::Equal; }
        else { return false; }
        pos_ += length;
        return true;
    }

    FormulaPtr parseComparison() {
        FormulaPtr left = parseAdditive();
        BinaryFormula::Op op;
        if (matchComparison(op)) {
            FormulaPtr right = parseAdditive();
            return std::make_unique<BinaryFormula>(op, std::move(left), std::move(right));
        }
        return left;
    }

    FormulaPtr parseAdditive() {
        FormulaPtr left = parseTerm();
        while (true) {
            BinaryFormula::Op op;
            if (match('+')) op = BinaryFormula::Op::Add;
            else if (match('-')) op = BinaryFormula::Op::Subtract;
            else return left;
            left = std::make_unique<BinaryFormula>(op, std::move(left), parseTerm());
        }
    }

    FormulaPtr parseTerm() {
        FormulaPtr left = parseUnary();
        while (true) {
            BinaryFormula::Op op;
            if (match('*')) op = BinaryFormula::Op::Multiply;
            else if (match('/')) op = BinaryFormula::Op::Divide;
            else return left;
            left = std::make_unique<BinaryFormula>(op, std::move(left), parseUnary());
        }
    }

    FormulaPtr parseUnary() {
        if (match('-')) {
            return std::make_unique<NegateFormula>(parseUnary());
        }
        if (match('+')) {
            return parseUnary();
        }
        return parsePrimary();
    }

    FormulaPtr parsePrimary() {
        skipSpace();
        if (pos_ >= src_.size()) {
            fail("unexpected end of formula");
        }
        const char c = src_[pos_];
        if (atDigit() || c == '.') {
            return parseNumber();
        }
        if (c == '"') {
            return parseText();
        }
        if (match('(')) {
            FormulaPtr inner = parseComparison();
            expect(')');
            return inner;
        }
        if (atAlpha()) {
            return parseName();
        }
        fail(std::string("unexpected '") + c + "'");
    }

    FormulaPtr parseNumber() {
        const std::size_t start = pos_;
        while (atDigit()) ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            ++pos_;
            while (atDigit()) ++pos_;
        }
        if (pos_ - start == 1 && src_[start] == '.') {
            fail("'.' is not a number");
        }
        try {
            return std::make_unique<NumberFormula>(std::stod(src_.substr(start, pos_ - start)));
        } catch (const std::out_of_range&) {
            fail("number is too large");
        }
    }

    // "text", with "" for a literal quote, as in Excel.
    FormulaPtr parseText() {
        ++pos_;
        std::string text;
        while (true) {
            if (pos_ >= src_.size()) {
                fail("text is missing its closing '\"'");
            }
            const char c = src_[pos_++];
            if (c != '"') {
                text += c;
            } else if (pos_ < src_.size() && src_[pos_] == '"') {
                text += '"';
                ++pos_;
            } else {
                return std::make_unique<TextFormula>(std::move(text));
            }
        }
    }

    // Letters then digits is a cell reference; letters then '(' is a function call.
    FormulaPtr parseName() {
        const std::size_t start = pos_;
        while (atAlpha()) ++pos_;
        if (atDigit()) {
            while (atDigit()) ++pos_;
            return std::make_unique<ReferenceFormula>(CellRef::parse(src_.substr(start, pos_ - start)));
        }
        std::string name = src_.substr(start, pos_ - start);
        for (char& ch : name) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        if (!match('(')) {
            fail("unknown name \"" + name + "\"");
        }
        return parseFunction(name);
    }

    // Called just after the opening '('.
    FormulaPtr parseFunction(const std::string& name) {
        if (name == "SUM" || name == "AVG" || name == "AVERAGE") {
            std::vector<FormulaPtr> args;
            do {
                args.push_back(parseArgument());
            } while (match(','));
            expect(')');
            if (name == "SUM") {
                return std::make_unique<SumFormula>(std::move(args));
            }
            return std::make_unique<AvgFormula>(std::move(args));
        }
        if (name == "IF") {
            FormulaPtr condition = parseComparison();
            expect(',');
            FormulaPtr whenTrue = parseComparison();
            expect(',');
            FormulaPtr whenFalse = parseComparison();
            expect(')');
            return std::make_unique<IfFormula>(std::move(condition), std::move(whenTrue), std::move(whenFalse));
        }
        fail("unknown function \"" + name + "\"");
    }

    // A SUM/AVG argument: either a range like A1:A3 or any expression.
    FormulaPtr parseArgument() {
        skipSpace();
        const std::size_t start = pos_;
        if (atAlpha()) {
            while (atAlpha()) ++pos_;
            const std::size_t digitsStart = pos_;
            while (atDigit()) ++pos_;
            if (pos_ > digitsStart) {
                const std::string first = src_.substr(start, pos_ - start);
                if (match(':')) {
                    skipSpace();
                    const std::size_t secondStart = pos_;
                    while (atAlpha()) ++pos_;
                    while (atDigit()) ++pos_;
                    const std::string second = src_.substr(secondStart, pos_ - secondStart);
                    if (second.empty()) {
                        fail("a range needs a cell after ':'");
                    }
                    return std::make_unique<RangeFormula>(CellRef::parse(first), CellRef::parse(second));
                }
            }
        }
        pos_ = start;
        return parseComparison();
    }
};

}  // namespace

FormulaPtr FormulaParser::parse(const std::string& source) {
    return Parser(source).parseFormula();
}
