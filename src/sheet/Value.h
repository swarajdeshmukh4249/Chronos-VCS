#pragma once

#include <string>
#include <variant>

// The result of evaluating a cell. std::monostate means the cell is empty,
// which counts as 0 in arithmetic.
using Value = std::variant<std::monostate, double, std::string>;

// 100 -> "100", 2.5 -> "2.5"
std::string formatNumber(double number);

std::string valueToString(const Value& value);
