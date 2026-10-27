#include "TestHarness.h"

#include <string>

#include "sheet/CSVHandler.h"
#include "sheet/Cell.h"
#include "sheet/Grid.h"
#include "sheet/SheetEvaluator.h"

// Issue #30
TEST(parsesSimpleCsv) {
    const Grid<Cell> grid = CSVHandler::parse("Item,Cost\nRent,1200\nTotal,=SUM(B2:B2)\n");
    CHECK(grid.rowCount() == 3);
    CHECK(grid.colCount() == 2);
    CHECK(grid.get("A1").getText() == "Item");
    CHECK(grid.get("B2").getNumber() == 1200);
    CHECK(grid.get("B3").isFormula());
    CHECK(std::get<double>(SheetEvaluator(grid).evaluate("B3")) == 1200);
}

TEST(parsesQuotedFields) {
    const Grid<Cell> grid = CSVHandler::parse("\"Smith, Jo\",\"say \"\"hi\"\"\",\"two\nlines\"\r\nx,,z\r\n");
    CHECK(grid.get("A1").getText() == "Smith, Jo");
    CHECK(grid.get("B1").getText() == "say \"hi\"");
    CHECK(grid.get("C1").getText() == "two\nlines");
    CHECK(grid.get("A2").getText() == "x");
    CHECK(grid.get("B2").isEmpty());
    CHECK(grid.get("C2").getText() == "z");
    CHECK(grid.rowCount() == 2);
}

TEST(handlesLineEndingsAndByteOrderMark) {
    for (const char* csv : {"1,2\n3,4", "1,2\r\n3,4\r\n", "1,2\r3,4\r", "\xEF\xBB\xBF" "1,2\n3,4\n"}) {
        const Grid<Cell> grid = CSVHandler::parse(csv);
        CHECK(grid.rowCount() == 2);
        CHECK(grid.get("A1").getNumber() == 1);
        CHECK(grid.get("B2").getNumber() == 4);
    }
}

TEST(emptyInputGivesEmptyGrid) {
    CHECK(CSVHandler::parse("").rowCount() == 0);
    CHECK(CSVHandler::parse("\xEF\xBB\xBF").rowCount() == 0);
}

TEST(unclosedQuoteRunsToTheEnd) {
    const Grid<Cell> grid = CSVHandler::parse("\"open,still\nopen");
    CHECK(grid.get("A1").getText() == "open,still\nopen");
}

TEST(formatsWithQuotingAndCrlf) {
    Grid<Cell> grid;
    grid.set("A1", "Name");
    grid.set("B1", 2.5);
    grid.set("A2", "Smith, Jo");
    grid.set("C2", "=A1&\"x\"");
    CHECK(CSVHandler::format(grid) == "Name,2.5,\r\n\"Smith, Jo\",,\"=A1&\"\"x\"\"\"\r\n");
}

TEST(savesAvgAsAverage) {
    Grid<Cell> grid;
    grid.set("A1", "=AVG(B1:B3)+avg (C1)");
    grid.set("A2", "=IF(A1>1, \"AVG(B1)\", AVERAGE(B1))");
    grid.set("A3", "=SUM(AVGX1)");
    CHECK(CSVHandler::format(grid) ==
          "=AVERAGE(B1:B3)+AVERAGE (C1)\r\n"
          "\"=IF(A1>1, \"\"AVG(B1)\"\", AVERAGE(B1))\"\r\n"
          "=SUM(AVGX1)\r\n");
}

TEST(roundTripKeepsEverything) {
    Grid<Cell> original;
    original.set("A1", "Budget");
    original.set("B1", 1e20);
    original.set("C1", -0.125);
    original.set("A2", "line one\nline two");
    original.set("B2", "=B1*2");
    original.set("D3", "\"quoted\"");

    const Grid<Cell> loaded = CSVHandler::parse(CSVHandler::format(original));
    CHECK(loaded.rowCount() == original.rowCount());
    CHECK(loaded.colCount() == original.colCount());
    for (int row = 0; row < original.rowCount(); ++row) {
        for (int col = 0; col < original.colCount(); ++col) {
            const Cell& before = original.get(CellRef{row, col});
            const Cell& after = loaded.get(CellRef{row, col});
            CHECK(before.getKind() == after.getKind());
            CHECK(before.getRaw() == after.getRaw());
        }
    }
    CHECK(loaded.get("B1").getNumber() == 1e20);
}

TEST(averageLoadsLikeAvg) {
    const Grid<Cell> grid = CSVHandler::parse("2,4,=AVERAGE(A1:B1)\n");
    CHECK(std::get<double>(SheetEvaluator(grid).evaluate("C1")) == 3);
}
