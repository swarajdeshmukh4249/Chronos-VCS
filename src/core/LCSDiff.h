#pragma once

#include "common/DiffAlgorithm.h"

// Line-by-line diff using the longest common subsequence: the lines both
// versions share stay put, and everything else is reported as a change.
//
// Each Change covers a run of neighbouring changed lines. before and after hold
// those lines, each ending in '\n', so an empty before means lines were only
// added and an empty after means they were only removed. location counts lines
// in the old version: "line 3", "lines 3-5", or "after line 2" for an insertion.
class LCSDiff : public DiffAlgorithm {
public:
    DiffResult diff(const Snapshot& a, const Snapshot& b) const override;
};
