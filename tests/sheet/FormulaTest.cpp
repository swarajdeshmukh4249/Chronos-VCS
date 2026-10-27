#include "TestHarness.h"

#include <string>

#include "common/Exceptions.h"
#include "sheet/Cell.h"
#include "sheet/FormulaParser.h"
#include "sheet/Grid.h"
#include "sheet/SheetEvaluator.h"

namespace {

bool isNumber(const Value& value, double expected) {
    const double* number = std::get_if<double>(&value);
    return number && *number == expected;
}

bool isText(const Value& value, const std::string& expected) {
    const std::string* text = std::get_if<std::string>(&value);
    return text && *text == expected;
}

// Puts formula in C9 of grid and evaluates it.
Value eval(Grid<Cell> grid, const std::string& formula) {
    grid.set("C9", formula);
    return SheetEvaluator(grid).evaluate("C9");
}

}  // namespace

// Issue #26
TEST(arithmeticWithReferences) {
    Grid<Cell> grid;
    grid.set("A1", 1);
    grid.set("B1", 3);
    CHECK(isNumber(eval(grid, "=A1+B1*2"), 7));
    CHECK(isNumber(eval(grid, "=(A1+B1)*2"), 8));
    CHECK(isNumber(eval(grid, "=B1/2"), 1.5));
    CHECK(isNumber(eval(grid, "=B1-A1-1"), 1));
    CHECK(isNumber(eval(grid, "=-A1+5"), 4));
    CHECK(isNumber(eval(grid, "= A1 * 2.5 "), 2.5));
}

TEST(emptyCellsCountAsZero) {
    Grid<Cell> grid;
    grid.set("A1", 4);
    CHECK(isNumber(eval(grid, "=A1+Z99"), 4));
}

TEST(formulasCanReferenceFormulas) {
    Grid<Cell> grid;
    grid.set("A1", 10);
    grid.set("B1", "=A1*2");
    grid.set("C1", "=B1+1");
    CHECK(isNumber(SheetEvaluator(grid).evaluate("C1"), 21));
}

TEST(divisionByZeroThrows) {
    Grid<Cell> grid;
    grid.set("A1", 5);
    CHECK_THROWS(eval(grid, "=A1/0"), DivisionByZeroException);
    CHECK_THROWS(eval(grid, "=A1/Z99"), DivisionByZeroException);
}

TEST(syntaxErrorsThrow) {
    Grid<Cell> grid;
    for (const char* bad : {"=", "=A1+", "=(A1", "=A1 B1", "=FOO(1)", "=\"abc", "=A1:A3", "=SUM()",
                            "=IF(1, 2)", "=1..2", "=.", "=hello", "=SUM(A1:)"}) {
        CHECK_THROWS(eval(grid, bad), FormulaSyntaxException);
    }
    CHECK_THROWS(FormulaParser::parse("A1+1"), FormulaSyntaxException);
}

TEST(badReferenceInFormulaThrows) {
    Grid<Cell> grid;
    CHECK_THROWS(eval(grid, "=A0+1"), InvalidCellReferenceException);
}

TEST(textInArithmeticThrows) {
    Grid<Cell> grid;
    grid.set("A1", "hello");
    CHECK_THROWS(eval(grid, "=A1+1"), ChronoException);
}

// Issue #27
TEST(sumAndAvg) {
    Grid<Cell> grid;
    grid.set("A1", 1);
    grid.set("A2", 2);
    grid.set("A3", 3);
    CHECK(isNumber(eval(grid, "=SUM(A1:A3)"), 6));
    CHECK(isNumber(eval(grid, "=SUM(A3:A1)"), 6));
    CHECK(isNumber(eval(grid, "=SUM(A1:A2, 10)"), 13));
    CHECK(isNumber(eval(grid, "=AVG(A1:A3)"), 2));
    CHECK(isNumber(eval(grid, "=AVERAGE(A1:A3)"), 2));
    CHECK(isNumber(eval(grid, "=sum(a1:a3)*2"), 12));
    CHECK(isNumber(eval(grid, "=SUM(A1+A2, A3)"), 6));
}

TEST(sumOverTwoDimensionalRange) {
    Grid<Cell> grid;
    grid.set("A1", 1);
    grid.set("B1", 2);
    grid.set("A2", 3);
    grid.set("B2", 4);
    CHECK(isNumber(eval(grid, "=SUM(A1:B2)"), 10));
}

TEST(aggregatesSkipTextAndEmptyCells) {
    Grid<Cell> grid;
    grid.set("A1", 4);
    grid.set("A2", "note");
    // A3 is empty
    grid.set("A4", 8);
    CHECK(isNumber(eval(grid, "=SUM(A1:A4)"), 12));
    CHECK(isNumber(eval(grid, "=AVG(A1:A4)"), 6));
}

TEST(avgOfNothingThrows) {
    Grid<Cell> grid;
    CHECK_THROWS(eval(grid, "=AVG(A1:A3)"), DivisionByZeroException);
}

TEST(ifWithComparisons) {
    Grid<Cell> grid;
    grid.set("A1", 10);
    CHECK(isNumber(eval(grid, "=IF(A1>5, 1, 2)"), 1));
    CHECK(isNumber(eval(grid, "=IF(A1<5, 1, 2)"), 2));
    CHECK(isNumber(eval(grid, "=IF(A1>=10, 1, 2)"), 1));
    CHECK(isNumber(eval(grid, "=IF(A1<=9, 1, 2)"), 2));
    CHECK(isNumber(eval(grid, "=IF(A1=10, 1, 2)"), 1));
    CHECK(isNumber(eval(grid, "=IF(A1<>10, 1, 2)"), 2));
    CHECK(isNumber(eval(grid, "=A1>5"), 1));
}

TEST(ifOnlyEvaluatesChosenBranch) {
    Grid<Cell> grid;
    grid.set("A1", 0);
    CHECK(isNumber(eval(grid, "=IF(A1=0, 0, 1/A1)"), 0));
}

TEST(textLiteralsAndComparisons) {
    Grid<Cell> grid;
    grid.set("A1", "yes");
    CHECK(isText(eval(grid, "=\"say \"\"hi\"\"\""), "say \"hi\""));
    CHECK(isNumber(eval(grid, "=IF(A1=\"yes\", 1, 2)"), 1));
    CHECK(isNumber(eval(grid, "=A1=5"), 0));
    CHECK_THROWS(eval(grid, "=A1>5"), ChronoException);
}

// The example from the work split: each branch sees a different C1.
TEST(overBudgetExample) {
    Grid<Cell> branchX;
    branchX.set("A1", 50);
    branchX.set("C1", "=A1*2");
    CHECK(isNumber(SheetEvaluator(branchX).evaluate("C1"), 100));

    Grid<Cell> branchY;
    branchY.set("A1", 10);
    branchY.set("C1", "=IF(A1>20, \"OVER BUDGET\", A1*2)");
    CHECK(isNumber(SheetEvaluator(branchY).evaluate("C1"), 20));

    Grid<Cell> merged = branchY;
    merged.set("A1", 50);
    CHECK(isText(SheetEvaluator(merged).evaluate("C1"), "OVER BUDGET"));
}

TEST(circularReferencesThrow) {
    Grid<Cell> grid;
    grid.set("A1", "=B1");
    grid.set("B1", "=A1");
    grid.set("C1", "=C1+1");
    grid.set("D1", "=SUM(D1:D3)");
    grid.set("E1", 7);
    SheetEvaluator evaluator(grid);
    CHECK_THROWS(evaluator.evaluate("A1"), CircularReferenceException);
    CHECK_THROWS(evaluator.evaluate("C1"), CircularReferenceException);
    CHECK_THROWS(evaluator.evaluate("D1"), CircularReferenceException);
    // The evaluator is still usable after a failure.
    CHECK(isNumber(evaluator.evaluate("E1"), 7));
    CHECK_THROWS(evaluator.evaluate("A1"), CircularReferenceException);
}

TEST(sharedReferenceIsNotCircular) {
    Grid<Cell> grid;
    grid.set("A1", 2);
    grid.set("B1", "=A1+A1");
    grid.set("C1", "=B1+A1*B1");
    CHECK(isNumber(SheetEvaluator(grid).evaluate("C1"), 12));
}
