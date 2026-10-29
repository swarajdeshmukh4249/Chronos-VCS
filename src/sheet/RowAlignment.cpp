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

// Above this many pairs in one run of changed rows, rows are paired in order instead.
const std::size_t kMaxRunPairs = 1'000'000;

// Matching first fields outweigh any number of other matching fields.
const std::uint64_t kSameKey = std::uint64_t{1} << 32;

// How alike an old and a new row are, for deciding whether the new row is an edit of
// the old one. The first field is usually the row's name ("Rent"), so a matching
// non-empty first field counts most; otherwise at least half the fields that could
// match must match. 0 means "not the same row".
//
// Only columns holding something on one side or the other are counted, because a
// column empty in both can never add to same and would otherwise make a row look
// less like itself than it is. A sheet such as "10,,=A1*2", whose middle column is
// spare, used to fail on that empty column alone: one of its two real columns
// matching is half, but one of three was not. A row both branches had edited was
// then read as deleted on one side and added on the other, and ended up in the
// file twice.
std::uint64_t likeness(const CSVHandler::Row& a, const CSVHandler::Row& b) {
    const std::size_t width = std::max(a.size(), b.size());
    std::uint64_t same = 0;
    std::size_t comparable = 0;
    for (std::size_t col = 0; col < width; ++col) {
        if (fieldAt(a, col).empty() && fieldAt(b, col).empty()) {
            continue;
        }
        ++comparable;
        if (!fieldAt(a, col).empty() && fieldAt(a, col) == fieldAt(b, col)) {
            ++same;
        }
    }
    if (!fieldAt(a, 0).empty() && fieldAt(a, 0) == fieldAt(b, 0)) {
        return kSameKey + same;
    }
    return same > 0 && 2 * same >= comparable ? same : 0;
}

// Ends a run of changed rows. Each removed row is paired with the added row most like
// it, keeping order, so inserting "Travel" above an edited "Rent" pairs Rent with Rent.
// Rows with no likely partner become plain removals or additions.
void flushRun(std::vector<RowMatch>& matches, std::vector<std::size_t>& removed, std::vector<std::size_t>& added,
              const CSVHandler::Rows& oldRows, const CSVHandler::Rows& newRows) {
    const std::size_t r = removed.size();
    const std::size_t a = added.size();
    if (r * a > kMaxRunPairs) {
        const std::size_t paired = std::min(r, a);
        for (std::size_t k = 0; k < paired; ++k) {
            matches.push_back({removed[k], added[k]});
        }
        for (std::size_t k = paired; k < r; ++k) {
            matches.push_back({removed[k], RowMatch::kNone});
        }
        for (std::size_t k = paired; k < a; ++k) {
            matches.push_back({RowMatch::kNone, added[k]});
        }
    } else {
        // best[i][j] = highest total likeness pairing removed[i..] with added[j..] in order.
        std::vector<std::uint64_t> score(r * a);
        for (std::size_t i = 0; i < r; ++i) {
            for (std::size_t j = 0; j < a; ++j) {
                score[i * a + j] = likeness(oldRows[removed[i]], newRows[added[j]]);
            }
        }
        std::vector<std::uint64_t> best((r + 1) * (a + 1), 0);
        auto at = [&](std::size_t i, std::size_t j) -> std::uint64_t& { return best[i * (a + 1) + j]; };
        for (std::size_t i = r; i-- > 0;) {
            for (std::size_t j = a; j-- > 0;) {
                const std::uint64_t s = score[i * a + j];
                at(i, j) = std::max({at(i + 1, j), at(i, j + 1), s > 0 ? s + at(i + 1, j + 1) : 0});
            }
        }

        std::size_t i = 0;
        std::size_t j = 0;
        while (i < r || j < a) {
            if (i < r && j < a && score[i * a + j] > 0 && at(i, j) == score[i * a + j] + at(i + 1, j + 1)) {
                matches.push_back({removed[i++], added[j++]});
            } else if (i < r && (j == a || at(i, j) == at(i + 1, j))) {
                matches.push_back({removed[i++], RowMatch::kNone});
            } else {
                matches.push_back({RowMatch::kNone, added[j++]});
            }
        }
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
                flushRun(matches, removed, added, oldRows, newRows);
                matches.push_back({prefix + i++, prefix + j++});
            } else if (j == m || (i < n && at(i + 1, j) >= at(i, j + 1))) {
                removed.push_back(prefix + i++);
            } else {
                added.push_back(prefix + j++);
            }
        }
    }
    flushRun(matches, removed, added, oldRows, newRows);

    for (std::size_t i = oldEnd, j = newEnd; i < oldRows.size(); ++i, ++j) {
        matches.push_back({i, j});
    }
    return matches;
}
