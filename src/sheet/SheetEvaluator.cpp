#include "sheet/SheetEvaluator.h"

#include "common/Exceptions.h"
#include "sheet/FormulaParser.h"

Value SheetEvaluator::valueAt(const CellRef& ref) const {
    const Cell& cell = grid_.get(ref);
    switch (cell.getKind()) {
    case Cell::Kind::Empty: return std::monostate{};
    case Cell::Kind::Number: return cell.getNumber();
    case Cell::Kind::Text: return cell.getText();
    case Cell::Kind::Formula: break;
    }

    if (!inProgress_.insert(ref).second) {
        throw CircularReferenceException("Circular reference involving " + ref.toString());
    }
    // Clears the mark even when evaluation throws, so the evaluator stays usable.
    struct Unmark {
        std::set<CellRef>& cells;
        CellRef ref;
        ~Unmark() { cells.erase(ref); }
    } unmark{inProgress_, ref};

    return FormulaParser::parse(cell.getText())->evaluate(*this);
}
