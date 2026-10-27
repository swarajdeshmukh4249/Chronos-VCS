#include "TestHarness.h"

#include "sheet/Cell.h"

TEST(defaultCellIsEmpty) {
    Cell cell;
    CHECK(cell.isEmpty());
    CHECK(cell.getRaw().empty());
}

TEST(numberConstructor) {
    Cell cell(10.5);
    CHECK(cell.getKind() == Cell::Kind::Number);
    CHECK(cell.getNumber() == 10.5);
    CHECK(cell.getRaw() == "10.5");
}

TEST(textConstructor) {
    Cell cell(std::string("Budget"));
    CHECK(cell.getKind() == Cell::Kind::Text);
    CHECK(cell.getText() == "Budget");
    CHECK(cell.getRaw() == "Budget");
}

TEST(formulaConstructor) {
    Cell cell("=A1*2");
    CHECK(cell.isFormula());
    CHECK(cell.getText() == "=A1*2");
    CHECK(cell.getRaw() == "=A1*2");
}

TEST(numericTextBecomesNumber) {
    CHECK(Cell("42").getKind() == Cell::Kind::Number);
    CHECK(Cell("42").getNumber() == 42);
    CHECK(Cell("-3.5").getNumber() == -3.5);
    CHECK(Cell("100").getRaw() == "100");
}

TEST(nonNumericTextStaysText) {
    CHECK(Cell("12abc").getKind() == Cell::Kind::Text);
    CHECK(Cell("1.2.3").getKind() == Cell::Kind::Text);
    CHECK(Cell("-").getKind() == Cell::Kind::Text);
    CHECK(Cell("inf").getKind() == Cell::Kind::Text);
}

TEST(emptyStringIsEmpty) {
    CHECK(Cell("").isEmpty());
}
