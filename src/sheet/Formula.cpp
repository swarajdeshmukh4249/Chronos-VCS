#include "sheet/Formula.h"

#include <algorithm>

#include "common/Exceptions.h"

namespace {

double toNumber(const Value& value) {
    if (const double* number = std::get_if<double>(&value)) {
        return *number;
    }
    if (const std::string* text = std::get_if<std::string>(&value)) {
        throw TypeMismatchException("Expected a number but got text \"" + *text + "\"");
    }
    return 0.0;
}

Value compare(BinaryFormula::Op op, const Value& a, const Value& b) {
    using Op = BinaryFormula::Op;
    const bool textA = std::holds_alternative<std::string>(a);
    const bool textB = std::holds_alternative<std::string>(b);

    int order;
    if (textA || textB) {
        if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
            if (op == Op::Equal) return 0.0;
            if (op == Op::NotEqual) return 1.0;
            throw TypeMismatchException("Cannot compare text with a number");
        }
        // An empty cell compares like "".
        const std::string none;
        const std::string& left = textA ? std::get<std::string>(a) : none;
        const std::string& right = textB ? std::get<std::string>(b) : none;
        order = left.compare(right);
    } else {
        const double left = toNumber(a);
        const double right = toNumber(b);
        order = left < right ? -1 : (left > right ? 1 : 0);
    }

    bool result = false;
    switch (op) {
    case Op::Equal: result = order == 0; break;
    case Op::NotEqual: result = order != 0; break;
    case Op::Less: result = order < 0; break;
    case Op::LessEqual: result = order <= 0; break;
    case Op::Greater: result = order > 0; break;
    case Op::GreaterEqual: result = order >= 0; break;
    default: break;
    }
    return result ? 1.0 : 0.0;
}

}  // namespace

void Formula::collectValues(const CellResolver& cells, std::vector<Value>& out) const {
    out.push_back(evaluate(cells));
}

Value NumberFormula::evaluate(const CellResolver&) const {
    return value_;
}

Value TextFormula::evaluate(const CellResolver&) const {
    return text_;
}

Value ReferenceFormula::evaluate(const CellResolver& cells) const {
    return cells.valueAt(ref_);
}

RangeFormula::RangeFormula(CellRef from, CellRef to)
    : topLeft_{std::min(from.row, to.row), std::min(from.col, to.col)},
      bottomRight_{std::max(from.row, to.row), std::max(from.col, to.col)} {}

Value RangeFormula::evaluate(const CellResolver&) const {
    throw FormulaSyntaxException("A range like " + topLeft_.toString() + ":" + bottomRight_.toString() +
                                 " can only be used inside SUM or AVG");
}

void RangeFormula::collectValues(const CellResolver& cells, std::vector<Value>& out) const {
    for (int row = topLeft_.row; row <= bottomRight_.row; ++row) {
        for (int col = topLeft_.col; col <= bottomRight_.col; ++col) {
            out.push_back(cells.valueAt(CellRef{row, col}));
        }
    }
}

Value NegateFormula::evaluate(const CellResolver& cells) const {
    return -toNumber(operand_->evaluate(cells));
}

Value BinaryFormula::evaluate(const CellResolver& cells) const {
    const Value a = left_->evaluate(cells);
    const Value b = right_->evaluate(cells);
    switch (op_) {
    case Op::Add: return toNumber(a) + toNumber(b);
    case Op::Subtract: return toNumber(a) - toNumber(b);
    case Op::Multiply: return toNumber(a) * toNumber(b);
    case Op::Divide: {
        const double divisor = toNumber(b);
        if (divisor == 0.0) {
            throw DivisionByZeroException("Division by zero");
        }
        return toNumber(a) / divisor;
    }
    default: return compare(op_, a, b);
    }
}

std::vector<double> AggregateFormula::numbers(const CellResolver& cells) const {
    std::vector<Value> values;
    for (const FormulaPtr& arg : args_) {
        arg->collectValues(cells, values);
    }
    std::vector<double> result;
    for (const Value& value : values) {
        if (const double* number = std::get_if<double>(&value)) {
            result.push_back(*number);
        }
    }
    return result;
}

Value SumFormula::evaluate(const CellResolver& cells) const {
    double total = 0.0;
    for (double number : numbers(cells)) {
        total += number;
    }
    return total;
}

Value AvgFormula::evaluate(const CellResolver& cells) const {
    const std::vector<double> values = numbers(cells);
    if (values.empty()) {
        throw DivisionByZeroException("AVG has no numbers to average");
    }
    double total = 0.0;
    for (double number : values) {
        total += number;
    }
    return total / static_cast<double>(values.size());
}

Value IfFormula::evaluate(const CellResolver& cells) const {
    const bool condition = toNumber(condition_->evaluate(cells)) != 0.0;
    return condition ? whenTrue_->evaluate(cells) : whenFalse_->evaluate(cells);
}
