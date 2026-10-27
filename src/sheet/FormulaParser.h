#pragma once

#include <string>

#include "sheet/Formula.h"

// Turns "=A1+B1*2" into a Formula tree. Supported: numbers, "text", cell
// references, + - * /, comparisons (= <> < <= > >=), parentheses,
// SUM(...), AVG(...) and IF(condition, then, else). Names are case-insensitive.
class FormulaParser {
public:
    // Throws FormulaSyntaxException, or InvalidCellReferenceException for refs like "A0".
    static FormulaPtr parse(const std::string& source);
};
