#include "core/LCSDiff.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// Above this many table cells (e.g. two 2,000-line middles), the changed middle
// is reported as one replacement instead of building a huge table.
const std::size_t kMaxTableCells = 4'000'000;

enum class Op { Keep, Remove, Add };

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// The edit script turning old[from, oldEnd) into new[from, newEnd), in order.
std::vector<Op> editScript(const std::vector<std::string>& oldLines, const std::vector<std::string>& newLines,
                           std::size_t from, std::size_t oldEnd, std::size_t newEnd) {
    const std::size_t n = oldEnd - from;
    const std::size_t m = newEnd - from;
    std::vector<Op> ops;
    if (n == 0 || m == 0 || (n + 1) * (m + 1) > kMaxTableCells) {
        ops.insert(ops.end(), n, Op::Remove);
        ops.insert(ops.end(), m, Op::Add);
        return ops;
    }

    // lengths[i][j] = LCS length of old[i..] and new[j..], stored row by row.
    std::vector<std::uint32_t> lengths((n + 1) * (m + 1), 0);
    auto at = [&](std::size_t i, std::size_t j) -> std::uint32_t& { return lengths[i * (m + 1) + j]; };
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            at(i, j) = oldLines[from + i] == newLines[from + j] ? at(i + 1, j + 1) + 1
                                                                 : std::max(at(i + 1, j), at(i, j + 1));
        }
    }

    std::size_t i = 0;
    std::size_t j = 0;
    while (i < n && j < m) {
        if (oldLines[from + i] == newLines[from + j]) {
            ops.push_back(Op::Keep);
            ++i;
            ++j;
        } else if (at(i + 1, j) >= at(i, j + 1)) {
            ops.push_back(Op::Remove);
            ++i;
        } else {
            ops.push_back(Op::Add);
            ++j;
        }
    }
    ops.insert(ops.end(), n - i, Op::Remove);
    ops.insert(ops.end(), m - j, Op::Add);
    return ops;
}

std::string describe(std::size_t firstOld, std::size_t removed) {
    if (removed == 0) {
        return firstOld == 1 ? "at the start" : "after line " + std::to_string(firstOld - 1);
    }
    if (removed == 1) {
        return "line " + std::to_string(firstOld);
    }
    return "lines " + std::to_string(firstOld) + "-" + std::to_string(firstOld + removed - 1);
}

}  // namespace

DiffResult LCSDiff::diff(const Snapshot& a, const Snapshot& b) const {
    const std::vector<std::string> oldLines = splitLines(a.getContent());
    const std::vector<std::string> newLines = splitLines(b.getContent());

    // Most edits touch a small part of a file, so skip the shared start and end first.
    std::size_t prefix = 0;
    while (prefix < oldLines.size() && prefix < newLines.size() && oldLines[prefix] == newLines[prefix]) {
        ++prefix;
    }
    std::size_t oldEnd = oldLines.size();
    std::size_t newEnd = newLines.size();
    while (oldEnd > prefix && newEnd > prefix && oldLines[oldEnd - 1] == newLines[newEnd - 1]) {
        --oldEnd;
        --newEnd;
    }

    DiffResult result;
    std::size_t oldPos = prefix;
    std::size_t newPos = prefix;
    Change current;
    std::size_t removed = 0;
    std::size_t firstOld = 0;
    auto flush = [&]() {
        if (!current.before.empty() || !current.after.empty()) {
            current.location = describe(firstOld, removed);
            result.changes.push_back(current);
        }
        current = Change{};
        removed = 0;
    };

    for (Op op : editScript(oldLines, newLines, prefix, oldEnd, newEnd)) {
        if (op == Op::Keep) {
            flush();
            ++oldPos;
            ++newPos;
            continue;
        }
        if (current.before.empty() && current.after.empty()) {
            firstOld = oldPos + 1;
        }
        if (op == Op::Remove) {
            current.before += oldLines[oldPos++] + "\n";
            ++removed;
        } else {
            current.after += newLines[newPos++] + "\n";
        }
    }
    flush();
    return result;
}
