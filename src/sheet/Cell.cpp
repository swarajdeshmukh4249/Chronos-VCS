#include "sheet/Cell.h"

#include <cctype>
#include <stdexcept>

#include "sheet/Value.h"

namespace {

// Strict on purpose: only [+-]digits[.digits][e[+-]digits], so "inf", "0x10" and " 42"
// stay text. The exponent is allowed because formatNumber writes large numbers as "1e+20".
bool parseNumber(const std::string& text, double& out) {
    std::size_t i = 0;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        ++i;
    }
    std::size_t digits = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        ++i;
        ++digits;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            ++i;
            ++digits;
        }
    }
    if (digits > 0 && i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            ++i;
        }
        const std::size_t exponentStart = i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i == exponentStart) {
            return false;
        }
    }
    if (digits == 0 || i != text.size()) {
        return false;
    }
    try {
        out = std::stod(text);
    } catch (const std::out_of_range&) {
        return false;
    }
    return true;
}

}  // namespace

Cell::Cell() : kind_(Kind::Empty), number_(0.0) {}

Cell::Cell(double number) : kind_(Kind::Number), number_(number) {}

Cell::Cell(const std::string& input) : kind_(Kind::Text), number_(0.0), text_(input) {
    if (input.empty()) {
        kind_ = Kind::Empty;
    } else if (input[0] == '=') {
        kind_ = Kind::Formula;
    } else if (parseNumber(input, number_)) {
        kind_ = Kind::Number;
        text_.clear();
    }
}

Cell::Cell(const char* input) : Cell(std::string(input)) {}

std::string Cell::getRaw() const {
    return kind_ == Kind::Number ? formatNumber(number_) : text_;
}
