#include "core/LineMerge.h"

#include <algorithm>
#include <string>
#include <vector>

#include "core/LineHunks.h"

namespace {

using Lines = std::vector<std::string>;

std::string join(const Lines& lines, std::size_t from, std::size_t to) {
    std::string text;
    for (std::size_t i = from; i < to; ++i) {
        text += lines[i];
    }
    return text;
}

// base[from, to) with the given hunks (all inside that range) applied.
std::string apply(const Lines& base, std::size_t from, std::size_t to, const std::vector<LineHunk>& hunks,
                  std::size_t first, std::size_t last) {
    std::string text;
    std::size_t pos = from;
    for (std::size_t h = first; h < last; ++h) {
        text += join(base, pos, hunks[h].oldStart);
        text += join(hunks[h].newLines, 0, hunks[h].newLines.size());
        pos = hunks[h].oldEnd;
    }
    return text + join(base, pos, to);
}

// Neighbouring changes count as touching, as in Git, so "a.end == b.start" overlaps.
bool touches(const LineHunk& hunk, std::size_t from, std::size_t to) {
    return hunk.oldStart <= to && from <= hunk.oldEnd;
}

// Base lines [from, to), counted from 0, described for people.
std::string describe(std::size_t from, std::size_t to) {
    if (from == to) {
        return from == 0 ? "at the start" : "after line " + std::to_string(from);
    }
    if (to - from == 1) {
        return "line " + std::to_string(from + 1);
    }
    return "lines " + std::to_string(from + 1) + "-" + std::to_string(to);
}

std::string endWithNewline(std::string text) {
    if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }
    return text;
}

}  // namespace

MergeResult LineMerge::merge(const Snapshot& base, const Snapshot& ours, const Snapshot& theirs) const {
    const Lines baseLines = splitLinesKeepingEnds(base.getContent());
    const std::vector<LineHunk> ourHunks = lineHunks(baseLines, splitLinesKeepingEnds(ours.getContent()));
    const std::vector<LineHunk> theirHunks = lineHunks(baseLines, splitLinesKeepingEnds(theirs.getContent()));

    MergeResult result;
    std::size_t pos = 0;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < ourHunks.size() || j < theirHunks.size()) {
        // Start a group at the earliest change, then pull in every change that touches it.
        const bool oursFirst =
            j == theirHunks.size() || (i < ourHunks.size() && ourHunks[i].oldStart <= theirHunks[j].oldStart);
        const LineHunk& first = oursFirst ? ourHunks[i] : theirHunks[j];
        std::size_t from = first.oldStart;
        std::size_t to = first.oldEnd;
        const std::size_t ourFirst = i;
        const std::size_t theirFirst = j;
        (oursFirst ? i : j)++;
        for (bool grew = true; grew;) {
            grew = false;
            if (i < ourHunks.size() && touches(ourHunks[i], from, to)) {
                to = std::max(to, ourHunks[i++].oldEnd);
                grew = true;
            }
            if (j < theirHunks.size() && touches(theirHunks[j], from, to)) {
                to = std::max(to, theirHunks[j++].oldEnd);
                grew = true;
            }
        }

        result.mergedContent += join(baseLines, pos, from);
        const std::string ourText = apply(baseLines, from, to, ourHunks, ourFirst, i);
        const std::string theirText = apply(baseLines, from, to, theirHunks, theirFirst, j);
        if (j == theirFirst || ourText == theirText) {
            result.mergedContent += ourText;  // only we changed it, or both made the same change
        } else if (i == ourFirst) {
            result.mergedContent += theirText;
        } else {
            Conflict conflict;
            conflict.location = describe(from, to);
            conflict.base = join(baseLines, from, to);
            conflict.ours = ourText;
            conflict.theirs = theirText;
            conflict.explanation = from == to ? "Both versions added different lines " + conflict.location + "."
                                              : "Both versions changed " + conflict.location + " in different ways.";
            result.conflicts.push_back(conflict);
            result.mergedContent += "<<<<<<< ours\n" + endWithNewline(ourText) + "=======\n" +
                                    endWithNewline(theirText) + ">>>>>>> theirs\n";
        }
        pos = to;
    }
    result.mergedContent += join(baseLines, pos, baseLines.size());
    return result;
}
