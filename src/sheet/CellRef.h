#pragma once

#include <string>

// A zero-based cell position: "A1" is {0, 0}, "B3" is {2, 1}.
struct CellRef {
    int row = 0;
    int col = 0;

    // Accepts names like "B3" or "aa10". Throws InvalidCellReferenceException otherwise.
    static CellRef parse(const std::string& name);

    std::string toString() const;

    bool operator==(const CellRef& other) const { return row == other.row && col == other.col; }
    bool operator!=(const CellRef& other) const { return !(*this == other); }
    bool operator<(const CellRef& other) const {
        return row != other.row ? row < other.row : col < other.col;
    }
};
