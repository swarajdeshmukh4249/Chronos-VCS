#pragma once

#include <string>
#include <vector>

#include "sheet/CellRef.h"

// What a merge did to a sheet's formulas: for every formula cell in the merged sheet
// whose value differs from what one side had, its value before the merge (base), on
// each side, and after it.
//
// A cell is a semantic conflict when one side changed its formula, the other side
// changed a cell it reads (directly, through other cells or inside a range), and the
// merged value is one neither side ever saw. A line merge, and a plain cell merge,
// call that merge clean, because no cell was edited twice.
struct CellImpact {
    CellRef cell;
    std::string base, ours, theirs, merged;  // values, e.g. "20" or "OVER BUDGET"
    bool semantic = false;
    std::string explanation;  // plain English, set when semantic
};

class MergeImpactReport {
public:
    // The four versions of the sheet as CSV text. merged must hold no conflict markers.
    MergeImpactReport(const std::string& base, const std::string& ours, const std::string& theirs,
                      const std::string& merged);

    const std::vector<CellImpact>& impacts() const { return impacts_; }
    std::vector<CellImpact> semanticConflicts() const;

    // One line per impact, e.g. "C1: before 20, this branch 100, the other branch 20, merged OVER BUDGET".
    std::vector<std::string> lines() const;

private:
    std::vector<CellImpact> impacts_;
};
