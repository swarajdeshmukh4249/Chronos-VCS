#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "sheet/CellRef.h"

// A sparse table of T addressed by name ("B3") or CellRef.
// Positions that were never set read as a default-constructed T.
template <typename T>
class Grid {
public:
    const T& get(const CellRef& ref) const {
        auto it = cells_.find(ref);
        return it == cells_.end() ? empty_ : it->second;
    }

    const T& get(const std::string& name) const { return get(CellRef::parse(name)); }

    void set(const CellRef& ref, T value) {
        cells_[ref] = std::move(value);
        rows_ = std::max(rows_, ref.row + 1);
        cols_ = std::max(cols_, ref.col + 1);
    }

    void set(const std::string& name, T value) { set(CellRef::parse(name), std::move(value)); }

    // One past the last row / column ever set.
    int rowCount() const { return rows_; }
    int colCount() const { return cols_; }

private:
    std::map<CellRef, T> cells_;
    T empty_{};
    int rows_ = 0;
    int cols_ = 0;
};
