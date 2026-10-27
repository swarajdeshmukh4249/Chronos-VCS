#pragma once

#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "sheet/Cell.h"
#include "sheet/CellRange.h"
#include "sheet/CellRef.h"
#include "sheet/Grid.h"

// Which cells each formula reads, built once from a grid. Only formula cells are
// nodes; numbers, text and empty cells are leaves. A range such as A2:Z1048576 is kept
// as one range instead of millions of cells, so building the graph costs about as much
// as the formulas and cells actually in the sheet. Cycles are found by depth-first
// search with an explicit stack, so long chains can't overflow the call stack.
class DependencyGraph {
public:
    using DonePredicate = std::function<bool(const CellRef&)>;

    // A formula that doesn't parse is kept as a node with no dependencies.
    explicit DependencyGraph(const Grid<Cell>& grid);

    bool hasFormula(const CellRef& ref) const { return dependencies_.count(ref) != 0; }

    // What ref's formula reads, one entry per cell reference or range (A1, A2:B10), in
    // the order they first appear, without repeats.
    const std::vector<CellRange>& dependenciesOf(const CellRef& ref) const;
    // The formula cells that read ref directly, through a reference or a range that
    // contains it, in sheet order. ref doesn't have to hold anything: an empty B7 is
    // still read by =SUM(B2:B9), which is what a merge needs to know.
    std::vector<CellRef> dependentsOf(const CellRef& ref) const;
    // Every formula cell whose value can change when ref changes, directly or
    // through other cells.
    std::set<CellRef> affectedBy(const CellRef& ref) const;

    // The formula cells needed to evaluate cell, each after the cells it reads, ending
    // with cell itself if it holds a formula. Cells for which alreadyDone returns true
    // are left out, along with anything only they needed.
    // Throws CircularReferenceException naming the cycle, e.g. "A1 -> B1 -> A1".
    std::vector<CellRef> evaluationOrder(const CellRef& cell, const DonePredicate& alreadyDone = nullptr) const;
    // The same for every formula cell in the sheet, so this also checks the whole
    // sheet for cycles.
    std::vector<CellRef> evaluationOrder() const;

private:
    // The cells ref's formula must wait for: its single references, plus the formula
    // cells inside its ranges (the other cells in a range are leaves).
    const std::vector<CellRef>& inputsOf(const CellRef& ref) const;
    void visit(const CellRef& start, const DonePredicate& alreadyDone, std::set<CellRef>& finished,
               std::vector<CellRef>& order) const;

    std::map<CellRef, std::vector<CellRange>> dependencies_;
    std::map<CellRef, std::vector<CellRef>> inputs_;
    // Who reads each cell named on its own, e.g. A1 in =A1*2.
    std::map<CellRef, std::vector<CellRef>> cellReaders_;
    // Each multi-cell range some formula reads, with that formula.
    std::vector<std::pair<CellRange, CellRef>> rangeReaders_;
};
