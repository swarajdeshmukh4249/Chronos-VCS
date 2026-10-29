#pragma once

#include <string>
#include <vector>

struct Conflict {
    std::string location, ours, theirs, base, explanation;
    // True for a semantic conflict (DependencyAwareMerge): the file holds no markers,
    // but a merged value should be checked before committing.
    bool semantic = false;
};

struct MergeResult {
    std::string mergedContent;
    std::vector<Conflict> conflicts;
};
