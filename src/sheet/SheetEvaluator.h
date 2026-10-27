#pragma once

#include <exception>
#include <map>
#include <string>
#include <vector>

#include "sheet/Cell.h"
#include "sheet/Formula.h"
#include "sheet/Grid.h"

// Works out the value of any cell in a grid, following formula references.
//
// Each formula cell is parsed and evaluated at most once, and the result (or the
// error it threw) is remembered, so create a new evaluator after changing the grid.
// Dependencies are evaluated with an explicit stack rather than recursion, so long
// chains such as A2 = A1+1, A3 = A2+1, ... can't overflow the call stack.
class SheetEvaluator : public CellResolver {
public:
    explicit SheetEvaluator(const Grid<Cell>& grid) : grid_(grid) {}
    // Holds a reference, so a temporary grid would dangle.
    explicit SheetEvaluator(const Grid<Cell>&&) = delete;

    Value evaluate(const std::string& name) const { return valueAt(CellRef::parse(name)); }
    Value valueAt(const CellRef& ref) const override;

private:
    // Evaluates root and every formula cell it depends on, deepest first.
    // Throws CircularReferenceException naming the cycle, e.g. "A1 -> B1 -> A1".
    void evaluateFormulaCell(const CellRef& root) const;
    bool isDone(const CellRef& ref) const;
    std::vector<CellRef> dependenciesOf(const CellRef& ref) const;
    // Throws FormulaSyntaxException or InvalidCellReferenceException.
    const Formula& formulaAt(const CellRef& ref) const;

    const Grid<Cell>& grid_;
    mutable std::map<CellRef, FormulaPtr> formulas_;
    mutable std::map<CellRef, Value> values_;
    // A cell whose formula failed rethrows the same error each time it is read,
    // so an IF branch that isn't taken can still point at a broken cell.
    mutable std::map<CellRef, std::exception_ptr> errors_;
};
