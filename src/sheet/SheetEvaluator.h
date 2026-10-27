#pragma once

#include <exception>
#include <map>
#include <string>

#include "sheet/Cell.h"
#include "sheet/DependencyGraph.h"
#include "sheet/Formula.h"
#include "sheet/Grid.h"

// Works out the value of any cell in a grid, following formula references.
//
// Formula cells are evaluated in DependencyGraph order, so every cell a formula reads
// is already known and long chains can't overflow the call stack. Each formula cell
// is evaluated at most once and its value (or error) is remembered, so create a new
// evaluator after changing the grid.
//
// Errors name the cell where they happened, e.g. "C1: Can't divide by zero". A cell
// that reads a broken cell passes on that same message.
class SheetEvaluator : public CellResolver {
public:
    explicit SheetEvaluator(const Grid<Cell>& grid) : grid_(grid), graph_(grid) {}
    // Holds a reference, so a temporary grid would dangle.
    explicit SheetEvaluator(const Grid<Cell>&&) = delete;

    Value evaluate(const std::string& name) const { return valueAt(CellRef::parse(name)); }
    Value valueAt(const CellRef& ref) const override;

    const DependencyGraph& getGraph() const { return graph_; }

private:
    void evaluateFormulaCell(const CellRef& ref) const;
    bool isDone(const CellRef& ref) const { return values_.count(ref) || errors_.count(ref); }

    const Grid<Cell>& grid_;
    DependencyGraph graph_;
    mutable std::map<CellRef, Value> values_;
    // A cell whose formula failed rethrows the same error each time it is read,
    // so an IF branch that isn't taken can still point at a broken cell.
    mutable std::map<CellRef, std::exception_ptr> errors_;
    // Set when a formula fails because a cell it read had already failed, so the
    // error keeps the original cell's name instead of gaining another prefix.
    mutable bool readBrokenCell_ = false;
};
