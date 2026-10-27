#include "core/LCSDiff.h"

#include <string>
#include <vector>

#include "core/LineHunks.h"

namespace {

// firstOld counts from 1.
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

    DiffResult result;
    for (const LineHunk& hunk : lineHunks(oldLines, newLines)) {
        Change change;
        change.location = describe(hunk.oldStart + 1, hunk.oldEnd - hunk.oldStart);
        for (std::size_t i = hunk.oldStart; i < hunk.oldEnd; ++i) {
            change.before += oldLines[i] + "\n";
        }
        for (const std::string& line : hunk.newLines) {
            change.after += line + "\n";
        }
        result.changes.push_back(change);
    }
    return result;
}
