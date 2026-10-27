#include "sheet/RowAlignment.h"

#include <algorithm>
#include <cstdint>

namespace {

// Above this many table cells (e.g. two 2,000-row middles), the changed middle is
// paired row by row instead of building a huge table.
const std::size_t kMaxTableCells = 4'000'000;

std::size_t usedLength(const CSVHandler::Row& row) {
    std::size_t length = row.size();
    while (length > 0 && row[length - 1].empty()) {
        --length;
    }
    return length;
}

// Ends a run of changed rows: edited rows first, then the extra removals or additions.
void flushRun(std::vector<RowMatch>& matches, std::vector<std::size_t>& removed, std::vector<std::size_t>& added) {
    const std::size_t paired = std::min(removed.size(), added.size());
    for (std::size_t k = 0; k < paired; ++k) {
        matches.push_back({removed[k], added[k]});
    }
    for (std::size_t k = paired; k < removed.size(); ++k) {
        matches.push_back({removed[k], RowMatch::kNone});
    }
    for (std::size_t k = paired; k < added.size(); ++k) {
        matches.push_back({RowMatch::kNone, added[k]});
    }
    removed.clear();
    added.clear();
}

}  // namespace

bool sameRow(const CSVHandler::Row& a, const CSVHandler::Row& b) {
    const std::size_t length = usedLength(a);
    if (length != usedLength(b)) {
        return false;
    }
    for (std::size_t col = 0; col < length; ++col) {
        if (a[col] != b[col]) {
            return false;
        }
    }
    return true;
}

const std::string& fieldAt(const CSVHandler::Row& row, std::size_t col) {
    static const std::string empty;
    return col < row.size() ? row[col] : empty;
}

std::vector<RowMatch> alignRows(const CSVHandler::Rows& oldRows, const CSVHandler::Rows& newRows) {
    // Most edits touch a small part of a sheet, so skip the shared start and end first.
    std::size_t prefix = 0;
    while (prefix < oldRows.size() && prefix < newRows.size() && sameRow(oldRows[prefix], newRows[prefix])) {
        ++prefix;
    }
    std::size_t oldEnd = oldRows.size();
    std::size_t newEnd = newRows.size();
    while (oldEnd > prefix && newEnd > prefix && sameRow(oldRows[oldEnd - 1], newRows[newEnd - 1])) {
        --oldEnd;
        --newEnd;
    }

    std::vector<RowMatch> matches;
    for (std::size_t i = 0; i < prefix; ++i) {
        matches.push_back({i, i});
    }

    std::vector<std::size_t> removed;
    std::vector<std::size_t> added;
    const std::size_t n = oldEnd - prefix;
    const std::size_t m = newEnd - prefix;
    if (n == 0 || m == 0 || (n + 1) * (m + 1) > kMaxTableCells) {
        for (std::size_t i = prefix; i < oldEnd; ++i) {
            removed.push_back(i);
        }
        for (std::size_t j = prefix; j < newEnd; ++j) {
            added.push_back(j);
        }
    } else {
        // lengths[i][j] = LCS length of the middles from old row i and new row j on.
        std::vector<std::uint32_t> lengths((n + 1) * (m + 1), 0);
        auto at = [&](std::size_t i, std::size_t j) -> std::uint32_t& { return lengths[i * (m + 1) + j]; };
        for (std::size_t i = n; i-- > 0;) {
            for (std::size_t j = m; j-- > 0;) {
                at(i, j) = sameRow(oldRows[prefix + i], newRows[prefix + j]) ? at(i + 1, j + 1) + 1
                                                                             : std::max(at(i + 1, j), at(i, j + 1));
            }
        }

        std::size_t i = 0;
        std::size_t j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && sameRow(oldRows[prefix + i], newRows[prefix + j])) {
                flushRun(matches, removed, added);
                matches.push_back({prefix + i++, prefix + j++});
            } else if (j == m || (i < n && at(i + 1, j) >= at(i, j + 1))) {
                removed.push_back(prefix + i++);
            } else {
                added.push_back(prefix + j++);
            }
        }
    }
    flushRun(matches, removed, added);

    for (std::size_t i = oldEnd, j = newEnd; i < oldRows.size(); ++i, ++j) {
        matches.push_back({i, j});
    }
    return matches;
}
