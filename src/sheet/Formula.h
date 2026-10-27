#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sheet/CellRange.h"
#include "sheet/CellRef.h"
#include "sheet/Value.h"

// Formulas read other cells through this interface, so they don't depend on Grid.
class CellResolver {
public:
    virtual Value valueAt(const CellRef& ref) const = 0;
    // The cells in range that may hold something, in row order. Empty cells add
    // nothing to SUM or AVG, so a resolver that knows which cells are set can leave
    // the rest out; this default lists every cell.
    virtual std::vector<CellRef> cellsIn(const CellRange& range) const;
    virtual ~CellResolver() = default;
};

// Interpreter pattern: a parsed formula is a tree of Formula nodes.
class Formula {
public:
    virtual Value evaluate(const CellResolver& cells) const = 0;
    // The values this node adds to a function's argument list; a range adds one per
    // cell that holds something.
    virtual void collectValues(const CellResolver& cells, std::vector<Value>& out) const;
    // What this formula reads: one entry per reference, with a range kept whole.
    virtual void collectReferences(std::vector<CellRange>& out) const { (void)out; }
    virtual ~Formula() = default;
};

using FormulaPtr = std::unique_ptr<Formula>;

class NumberFormula : public Formula {
public:
    explicit NumberFormula(double value) : value_(value) {}
    Value evaluate(const CellResolver& cells) const override;

private:
    double value_;
};

class TextFormula : public Formula {
public:
    explicit TextFormula(std::string text) : text_(std::move(text)) {}
    Value evaluate(const CellResolver& cells) const override;

private:
    std::string text_;
};

class ReferenceFormula : public Formula {
public:
    explicit ReferenceFormula(CellRef ref) : ref_(ref) {}
    Value evaluate(const CellResolver& cells) const override;
    void collectReferences(std::vector<CellRange>& out) const override { out.push_back(CellRange::single(ref_)); }

private:
    CellRef ref_;
};

// A1:A3. Only valid as a function argument.
class RangeFormula : public Formula {
public:
    RangeFormula(CellRef from, CellRef to);
    Value evaluate(const CellResolver& cells) const override;
    void collectValues(const CellResolver& cells, std::vector<Value>& out) const override;
    void collectReferences(std::vector<CellRange>& out) const override;

private:
    CellRange range_;
};

class NegateFormula : public Formula {
public:
    explicit NegateFormula(FormulaPtr operand) : operand_(std::move(operand)) {}
    Value evaluate(const CellResolver& cells) const override;
    void collectReferences(std::vector<CellRange>& out) const override { operand_->collectReferences(out); }

private:
    FormulaPtr operand_;
};

// Arithmetic and comparisons. Comparisons give 1 for true and 0 for false.
class BinaryFormula : public Formula {
public:
    enum class Op { Add, Subtract, Multiply, Divide, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

    BinaryFormula(Op op, FormulaPtr left, FormulaPtr right)
        : op_(op), left_(std::move(left)), right_(std::move(right)) {}
    Value evaluate(const CellResolver& cells) const override;
    void collectReferences(std::vector<CellRange>& out) const override;

private:
    Op op_;
    FormulaPtr left_, right_;
};

// Shared by SUM and AVG.
class AggregateFormula : public Formula {
public:
    explicit AggregateFormula(std::vector<FormulaPtr> args) : args_(std::move(args)) {}
    void collectReferences(std::vector<CellRange>& out) const override;

protected:
    // Numbers from all arguments. Text and empty cells are skipped, as in Excel.
    std::vector<double> numbers(const CellResolver& cells) const;

private:
    std::vector<FormulaPtr> args_;
};

class SumFormula : public AggregateFormula {
public:
    using AggregateFormula::AggregateFormula;
    Value evaluate(const CellResolver& cells) const override;
};

class AvgFormula : public AggregateFormula {
public:
    using AggregateFormula::AggregateFormula;
    Value evaluate(const CellResolver& cells) const override;
};

class IfFormula : public Formula {
public:
    IfFormula(FormulaPtr condition, FormulaPtr whenTrue, FormulaPtr whenFalse)
        : condition_(std::move(condition)), whenTrue_(std::move(whenTrue)), whenFalse_(std::move(whenFalse)) {}
    Value evaluate(const CellResolver& cells) const override;
    void collectReferences(std::vector<CellRange>& out) const override;

private:
    FormulaPtr condition_, whenTrue_, whenFalse_;
};
