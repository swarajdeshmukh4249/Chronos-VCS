#pragma once

#include "common/MergeResult.h"
#include "common/Snapshot.h"

// Repository picks the strategy by file type:
// .csv -> DependencyAwareMerge, everything else -> LineMerge.
class MergeStrategy {
public:
    virtual MergeResult merge(const Snapshot& base,
                              const Snapshot& ours,
                              const Snapshot& theirs) const = 0;
    virtual ~MergeStrategy() = default;
};
