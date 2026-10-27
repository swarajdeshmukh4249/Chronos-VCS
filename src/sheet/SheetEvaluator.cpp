#include "sheet/SheetEvaluator.h"

#include "common/Exceptions.h"
#include "sheet/FormulaParser.h"

namespace {

// The same error, with "C1: " in front of its message.
std::exception_ptr inCell(const CellRef& cell, const std::exception_ptr& error) {
    const std::string prefix = cell.toString() + ": ";
    try {
        std::rethrow_exception(error);
    } catch (const DivisionByZeroException& e) {
        return std::make_exception_ptr(DivisionByZeroException(prefix + e.what()));
    } catch (const FormulaSyntaxException& e) {
        return std::make_exception_ptr(FormulaSyntaxException(prefix + e.what()));
    } catch (const InvalidCellReferenceException& e) {
        return std::make_exception_ptr(InvalidCellReferenceException(prefix + e.what()));
    } catch (const TypeMismatchException& e) {
        return std::make_exception_ptr(TypeMismatchException(prefix + e.what()));
    } catch (const NumberOverflowException& e) {
        return std::make_exception_ptr(NumberOverflowException(prefix + e.what()));
    } catch (const ChronoException& e) {
        return std::make_exception_ptr(ChronoException(prefix + e.what()));
    } catch (...) {
        return error;
    }
}

}  // namespace

Value SheetEvaluator::valueAt(const CellRef& ref) const {
    const Cell& cell = grid_.get(ref);
    switch (cell.getKind()) {
    case Cell::Kind::Empty: return std::monostate{};
    case Cell::Kind::Number: return cell.getNumber();
    case Cell::Kind::Text: return cell.getText();
    case Cell::Kind::Formula: break;
    }

    if (!isDone(ref)) {
        const auto done = [this](const CellRef& other) { return isDone(other); };
        for (const CellRef& next : graph_.evaluationOrder(ref, done)) {
            evaluateFormulaCell(next);
        }
    }
    auto error = errors_.find(ref);
    if (error != errors_.end()) {
        readBrokenCell_ = true;
        std::rethrow_exception(error->second);
    }
    return values_.at(ref);
}

void SheetEvaluator::evaluateFormulaCell(const CellRef& ref) const {
    // Every cell this formula reads is already done, so evaluation only reads remembered values.
    readBrokenCell_ = false;
    try {
        values_[ref] = FormulaParser::parse(grid_.get(ref).getText())->evaluate(*this);
    } catch (...) {
        errors_[ref] = readBrokenCell_ ? std::current_exception() : inCell(ref, std::current_exception());
    }
}
