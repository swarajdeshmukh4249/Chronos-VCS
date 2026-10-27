#include "TestHarness.h"

#include "common/Exceptions.h"
#include "sheet/Cell.h"
#include "sheet/CellRef.h"
#include "sheet/Grid.h"

TEST(parsesCellNames) {
    CHECK(CellRef::parse("A1") == (CellRef{0, 0}));
    CHECK(CellRef::parse("B3") == (CellRef{2, 1}));
    CHECK(CellRef::parse("Z1") == (CellRef{0, 25}));
    CHECK(CellRef::parse("AA10") == (CellRef{9, 26}));
    CHECK(CellRef::parse("b3") == (CellRef{2, 1}));
}

TEST(cellNamesRoundTrip) {
    for (const char* name : {"A1", "B3", "Z9", "AA10", "AZ5", "XFD1048576"}) {
        CHECK(CellRef::parse(name).toString() == name);
    }
}

TEST(rejectsInvalidCellNames) {
    for (const char* name : {"", "A", "1", "A0", "1A", "A1B", "ABCD1", "A12345678", "A-1", " A1"}) {
        CHECK_THROWS(CellRef::parse(name), InvalidCellReferenceException);
    }
}

TEST(gridReadsAndWritesByName) {
    Grid<int> grid;
    grid.set("B3", 5);
    CHECK(grid.get("B3") == 5);
    CHECK(grid.get(CellRef{2, 1}) == 5);
    CHECK(grid.get("C9") == 0);
    CHECK(grid.rowCount() == 3);
    CHECK(grid.colCount() == 2);
}

TEST(gridOverwrites) {
    Grid<int> grid;
    grid.set("A1", 1);
    grid.set("A1", 2);
    CHECK(grid.get("A1") == 2);
}

TEST(gridOfCells) {
    Grid<Cell> grid;
    grid.set("A1", 10);
    grid.set("B1", "=A1*2");
    CHECK(grid.get("A1").getNumber() == 10);
    CHECK(grid.get("B1").isFormula());
    CHECK(grid.get("Z99").isEmpty());
}

TEST(gridRejectsBadNames) {
    Grid<int> grid;
    CHECK_THROWS(grid.set("A0", 1), InvalidCellReferenceException);
    CHECK_THROWS(grid.get("hello"), InvalidCellReferenceException);
}
