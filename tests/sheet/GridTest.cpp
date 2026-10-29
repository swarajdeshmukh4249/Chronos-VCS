#include "TestHarness.h"

#include <vector>

#include "common/Exceptions.h"
#include "sheet/Cell.h"
#include "sheet/CellRange.h"
#include "sheet/CellRef.h"
#include "sheet/Grid.h"

namespace {

// Every set cell inside range, found the slow and obvious way.
std::vector<CellRef> byHand(const Grid<int>& grid, const CellRange& range) {
    std::vector<CellRef> found;
    for (const auto& [ref, value] : grid) {
        (void)value;
        if (range.contains(ref)) found.push_back(ref);
    }
    return found;
}

}  // namespace

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
    for (const char* name : {"", "A", "1", "A0", "1A", "A1B", "ABCD1", "A12345678", "A-1", " A1",
                             // Past Excel's limits (#45)
                             "XFE1", "ZZZ1", "A1048577", "A9999999"}) {
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

// setCellsIn picks one of two routes depending on how the range compares to the number
// of set cells, so both have to give the same answer. These ranges sit either side of
// that choice; the ones that pick the wrong route are slow, not wrong, and a mistake in
// either route would otherwise only show up as a wrong SUM on a sheet of some size.
TEST(setCellsInFindsTheSameCellsWhicheverRouteItTakes) {
    Grid<int> grid;
    for (int row = 0; row < 40; row += 3) {
        for (int col = 0; col < 12; col += 2) {
            grid.set(CellRef{row, col}, row * 100 + col);
        }
    }
    const std::vector<CellRange> ranges = {
        {{0, 0}, {0, 0}},          // one cell
        {{1, 1}, {2, 2}},          // nothing set inside
        {{5, 4}, {30, 9}},         // part of the block
        {{0, 0}, {39, 11}},        // all of it
        {{39, 11}, {39, 11}},      // the last cell
        {{40, 0}, {100, 20}},      // past the end
        {{0, 0}, {1048575, 25}},   // a whole column pair, far more rows than cells
    };
    for (const CellRange& range : ranges) {
        CHECK(grid.setCellsIn(range) == byHand(grid, range));
    }
}

// The two routes swap over somewhere around one row per set cell, so check across it.
TEST(setCellsInAgreesAcrossItsCrossoverPoint) {
    Grid<int> grid;
    for (int row = 0; row < 16; ++row) {
        grid.set(CellRef{row, 0}, row);
    }
    for (int lastRow = 0; lastRow < 70; ++lastRow) {
        const CellRange range{{0, 0}, {lastRow, 0}};
        CHECK(grid.setCellsIn(range) == byHand(grid, range));
    }
}

TEST(rangesPrintTheWayTheyAreWritten) {
    const CellRange wide{CellRef::parse("A2"), CellRef::parse("B10")};
    CHECK(wide.toString() == "A2:B10");
    CHECK(CellRange::single(CellRef::parse("B10")).toString() == "B10");
    CHECK(CellRange::single(CellRef::parse("B10")).isSingle());
    const CellRange range{CellRef::parse("B2"), CellRef::parse("C3")};
    CHECK(range.contains(CellRef::parse("B2")));
    CHECK(range.contains(CellRef::parse("C3")));
    CHECK(!range.contains(CellRef::parse("B4")));
    CHECK(!range.contains(CellRef::parse("D2")));
}
