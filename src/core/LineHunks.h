#pragma once

#include <cstddef>
#include <string>
#include <vector>

// One run of changed lines: old lines [oldStart, oldEnd) are replaced by
// newLines. Lines are counted from 0, and an insertion has oldStart == oldEnd.
struct LineHunk {
    std::size_t oldStart = 0;
    std::size_t oldEnd = 0;
    std::vector<std::string> newLines;
};

// "a\nb" -> {"a", "b"}. A final '\n' doesn't start another line.
std::vector<std::string> splitLines(const std::string& text);
// "a\nb" -> {"a\n", "b"}. Joining the lines gives the text back exactly.
std::vector<std::string> splitLinesKeepingEnds(const std::string& text);

// The changes that turn oldLines into newLines, in order. Lines both versions
// share are kept, using the longest common subsequence, so each hunk is as small
// as possible. Very large changed regions are reported as one replacement.
std::vector<LineHunk> lineHunks(const std::vector<std::string>& oldLines, const std::vector<std::string>& newLines);
