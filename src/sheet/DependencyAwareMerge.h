#pragma once

#include "common/MergeStrategy.h"

// CellMerge, plus the check a line merge can't make. When one side changed a formula
// and the other changed a cell it reads, the merged value is one neither person saw,
// so it is reported as a semantic conflict even though no cell was edited twice:
//
//   base:    A1 = 10, C1 = =A1*2
//   ours:    A1 = 50                              (C1 shows 100)
//   theirs:  C1 = =IF(A1>20,"OVER BUDGET",A1*2)  (C1 shows 20)
//   merged:  C1 shows OVER BUDGET
//
// The file keeps the merged values with no markers, since the merge is often right;
// the conflict makes someone look before it is committed. See MergeImpactReport.
class DependencyAwareMerge : public MergeStrategy {
public:
    MergeResult merge(const Snapshot& base, const Snapshot& ours, const Snapshot& theirs) const override;
};
