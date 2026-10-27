#pragma once

#include <functional>
#include <map>
#include <set>
#include <vector>

#include "sheet/Cell.h"
#include "sheet/CellRef.h"
#include "sheet/Grid.h"

// Which cells each formula reads, built once from a grid. Only formula cells are
// nodes; numbers, text and empty cells are leaves. Cycles are found by depth-first
// search with an explicit stack, so long chains can't overflow the call stack.
class DependencyGraph {
public:
    using DonePredicate = std::function<bool(const CellRef&)>;

    // A formula that doesn't parse is kept as a node with no dependencies.
    explicit DependencyGraph(const Grid<Cell>& grid);

    bool hasFormula(const CellRef& ref) const { return dependencies_.count(ref) != 0; }

    // The cells ref's formula reads, in the order they first appear, without repeats.
    const std::vector<CellRef>& dependenciesOf(const CellRef& ref) const;
    // The formula cells that read ref directly.
    const std::vector<CellRef>& dependentsOf(const CellRef& ref) const;
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
    void visit(const CellRef& start, const DonePredicate& alreadyDone, std::set<CellRef>& finished,
               std::vector<CellRef>& order) const;

    std::map<CellRef, std::vector<CellRef>> dependencies_;
    std::map<CellRef, std::vector<CellRef>> dependents_;
};
