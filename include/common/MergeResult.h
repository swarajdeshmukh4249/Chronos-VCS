#pragma once

#include <string>
#include <vector>

struct Conflict {
    std::string location, ours, theirs, base, explanation;
};

struct MergeResult {
    std::string mergedContent;
    std::vector<Conflict> conflicts;
};
