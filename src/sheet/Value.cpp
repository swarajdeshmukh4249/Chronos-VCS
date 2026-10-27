#include "sheet/Value.h"

#include <cstdio>

std::string formatNumber(double number) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.15g", number);
    return buffer;
}

std::string valueToString(const Value& value) {
    if (const double* number = std::get_if<double>(&value)) {
        return formatNumber(*number);
    }
    if (const std::string* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    return "";
}
