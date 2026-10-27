#pragma once

#include <string>

// One spreadsheet cell: empty, a number, text, or a formula such as "=A1*2".
class Cell {
public:
    enum class Kind { Empty, Number, Text, Formula };

    Cell();
    Cell(double number);
    // Without this, Cell(0) is ambiguous: a literal 0 also converts to const char*.
    Cell(int number) : Cell(static_cast<double>(number)) {}
    // "" is empty, "=..." is a formula, numeric text like "42" is a number,
    // and anything else is text.
    Cell(const std::string& input);
    Cell(const char* input);

    Kind getKind() const { return kind_; }
    bool isEmpty() const { return kind_ == Kind::Empty; }
    bool isFormula() const { return kind_ == Kind::Formula; }

    // 0 unless the cell holds a number.
    double getNumber() const { return number_; }
    // The text, or the formula source including its leading '='.
    const std::string& getText() const { return text_; }
    // What the user typed; this is what gets saved to CSV.
    std::string getRaw() const;

private:
    Kind kind_;
    double number_;
    std::string text_;
};
