#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "sheet/CellRef.h"

// A rectangle of cells such as A2:B10. A single cell is a range whose corners match.
struct CellRange {
    CellRef topLeft;
    CellRef bottomRight;

    static CellRange single(const CellRef& ref) { return {ref, ref}; }

    bool contains(const CellRef& ref) const {
        return ref.row >= topLeft.row && ref.row <= bottomRight.row && ref.col >= topLeft.col &&
               ref.col <= bottomRight.col;
    }
    bool isSingle() const { return topLeft == bottomRight; }
    // "A2:B10", or "A2" for a single cell.
    std::string toString() const {
        return isSingle() ? topLeft.toString() : topLeft.toString() + ":" + bottomRight.toString();
    }

    bool operator==(const CellRange& other) const {
        return topLeft == other.topLeft && bottomRight == other.bottomRight;
    }
    bool operator<(const CellRange& other) const {
        return topLeft != other.topLeft ? topLeft < other.topLeft : bottomRight < other.bottomRight;
    }
};

namespace detail {
inline const CellRef& keyOf(const CellRef& ref) { return ref; }
template <typename T>
const CellRef& keyOf(const std::pair<const CellRef, T>& entry) {
    return entry.first;
}
}  // namespace detail

// The keys of a CellRef-ordered std::map or std::set that fall inside range, in row
// order. Sheets are sparse, so this looks up each row of the range when it has fewer
// rows than there are keys, and otherwise checks every key: either way a whole-column
// range like A2:Z1048576 costs about as much as the cells actually set.
template <typename Container>
std::vector<CellRef> keysInRange(const Container& keys, const CellRange& range) {
    std::vector<CellRef> found;
    const auto rows = static_cast<std::size_t>(range.bottomRight.row - range.topLeft.row + 1);
    if (rows <= keys.size()) {
        for (int row = range.topLeft.row; row <= range.bottomRight.row; ++row) {
            for (auto it = keys.lower_bound(CellRef{row, range.topLeft.col});
                 it != keys.end() && detail::keyOf(*it).row == row && detail::keyOf(*it).col <= range.bottomRight.col;
                 ++it) {
                found.push_back(detail::keyOf(*it));
            }
        }
    } else {
        for (const auto& entry : keys) {
            if (range.contains(detail::keyOf(entry))) {
                found.push_back(detail::keyOf(entry));
            }
        }
    }
    return found;
}
