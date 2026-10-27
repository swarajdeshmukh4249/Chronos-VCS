#include "sheet/CellRef.h"

#include <cctype>

#include "common/Exceptions.h"

namespace {

// Same limits as Excel: columns up to XFD, rows up to 1,048,576.
const int kMaxColumns = 16384;
const int kMaxRows = 1048576;
// Checked first, so the conversions below can't overflow.
const std::size_t kMaxColumnLetters = 3;
const std::size_t kMaxRowDigits = 7;

[[noreturn]] void invalid(const std::string& name) {
    throw InvalidCellReferenceException("\"" + name +
                                        "\" is not a valid cell. Use a column letter and a row number, like B3.");
}

}  // namespace

CellRef CellRef::parse(const std::string& name) {
    std::size_t letters = 0;
    while (letters < name.size() && std::isalpha(static_cast<unsigned char>(name[letters]))) {
        ++letters;
    }
    std::size_t digits = 0;
    while (letters + digits < name.size() &&
           std::isdigit(static_cast<unsigned char>(name[letters + digits]))) {
        ++digits;
    }

    if (letters == 0 || letters > kMaxColumnLetters || digits == 0 || digits > kMaxRowDigits ||
        letters + digits != name.size()) {
        invalid(name);
    }

    int col = 0;
    for (std::size_t i = 0; i < letters; ++i) {
        col = col * 26 + (std::toupper(static_cast<unsigned char>(name[i])) - 'A' + 1);
    }
    int row = 0;
    for (std::size_t i = letters; i < name.size(); ++i) {
        row = row * 10 + (name[i] - '0');
    }
    if (row == 0 || row > kMaxRows || col > kMaxColumns) {
        invalid(name);
    }
    return CellRef{row - 1, col - 1};
}

std::string CellRef::toString() const {
    std::string letters;
    for (int n = col + 1; n > 0; n /= 26) {
        --n;
        letters.insert(letters.begin(), static_cast<char>('A' + n % 26));
    }
    return letters + std::to_string(row + 1);
}
