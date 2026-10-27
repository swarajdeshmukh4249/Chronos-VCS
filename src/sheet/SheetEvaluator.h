#pragma once

#include <set>
#include <string>

#include "sheet/Cell.h"
#include "sheet/Formula.h"
#include "sheet/Grid.h"

// Works out the value of any cell in a grid, following formula references.
class SheetEvaluator : public CellResolver {
public:
    explicit SheetEvaluator(const Grid<Cell>& grid) : grid_(grid) {}
    // Holds a reference, so a temporary grid would dangle.
    explicit SheetEvaluator(const Grid<Cell>&&) = delete;

    Value evaluate(const std::string& name) const { return valueAt(CellRef::parse(name)); }
    Value valueAt(const CellRef& ref) const override;

private:
    const Grid<Cell>& grid_;
    // Formula cells currently being evaluated, so A1 -> B1 -> A1 throws instead of
    // recursing forever. Full DFS cycle detection comes with DependencyGraph in phase 2.
    mutable std::set<CellRef> inProgress_;
};
