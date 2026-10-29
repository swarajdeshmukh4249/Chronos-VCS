#include "sheet/DependencyAwareMerge.h"

#include "sheet/CellMerge.h"
#include "sheet/MergeImpactReport.h"

MergeResult DependencyAwareMerge::merge(const Snapshot& base, const Snapshot& ours, const Snapshot& theirs) const {
    MergeResult result = CellMerge().merge(base, ours, theirs);
    // A cell still holding markers can't be evaluated, and must be fixed first anyway.
    if (!result.conflicts.empty()) {
        return result;
    }
    const MergeImpactReport report(base.getContent(), ours.getContent(), theirs.getContent(), result.mergedContent);
    for (const CellImpact& impact : report.semanticConflicts()) {
        Conflict conflict;
        conflict.location = impact.cell.toString();
        conflict.base = impact.base;
        conflict.ours = impact.ours;
        conflict.theirs = impact.theirs;
        conflict.explanation = impact.explanation;
        conflict.semantic = true;
        result.conflicts.push_back(conflict);
    }
    return result;
}
