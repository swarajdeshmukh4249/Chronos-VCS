#include "sheet/SheetEvaluator.h"

#include <set>

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

    if (!isDone(ref)) {
        evaluateFormulaCell(ref);
    }
    auto error = errors_.find(ref);
    if (error != errors_.end()) {
        std::rethrow_exception(error->second);
    }
    return values_.at(ref);
}

void SheetEvaluator::evaluateFormulaCell(const CellRef& root) const {
    struct Frame {
        CellRef ref;
        bool expanded;
    };
    std::vector<Frame> stack{{root, false}};
    // Expanded cells whose dependencies are still being evaluated: the current path.
    std::set<CellRef> onPath;

    while (!stack.empty()) {
        const CellRef ref = stack.back().ref;
        if (isDone(ref)) {
            stack.pop_back();
            continue;
        }

        if (!stack.back().expanded) {
            stack.back().expanded = true;
            onPath.insert(ref);
            for (const CellRef& dependency : dependenciesOf(ref)) {
                if (!grid_.get(dependency).isFormula() || isDone(dependency)) {
                    continue;
                }
                if (onPath.count(dependency)) {
                    // The expanded frames, bottom to top, are the path; the cycle starts at dependency.
                    std::string cycle;
                    for (const Frame& frame : stack) {
                        if (frame.expanded && (!cycle.empty() || frame.ref == dependency)) {
                            cycle += frame.ref.toString() + " -> ";
                        }
                    }
                    throw CircularReferenceException("Circular reference: " + cycle + dependency.toString());
                }
                stack.push_back({dependency, false});
            }
            continue;
        }

        // Every dependency is now done, so evaluating ref only reads remembered values.
        stack.pop_back();
        onPath.erase(ref);
        try {
            values_[ref] = formulaAt(ref).evaluate(*this);
        } catch (const CircularReferenceException&) {
            throw;
        } catch (...) {
            errors_[ref] = std::current_exception();
        }
    }
}

bool SheetEvaluator::isDone(const CellRef& ref) const {
    return values_.count(ref) || errors_.count(ref);
}

std::vector<CellRef> SheetEvaluator::dependenciesOf(const CellRef& ref) const {
    std::vector<CellRef> references;
    try {
        formulaAt(ref).collectReferences(references);
    } catch (const ChronoException&) {
        // A formula that doesn't parse has no dependencies; evaluating it reports the error.
    }
    return references;
}

const Formula& SheetEvaluator::formulaAt(const CellRef& ref) const {
    auto it = formulas_.find(ref);
    if (it == formulas_.end()) {
        it = formulas_.emplace(ref, FormulaParser::parse(grid_.get(ref).getText())).first;
    }
    return *it->second;
}
