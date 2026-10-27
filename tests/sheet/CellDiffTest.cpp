#include "TestHarness.h"

#include <string>

#include "common/Snapshot.h"
#include "sheet/CSVHandler.h"
#include "sheet/CellDiff.h"

namespace {

DiffResult diffSheets(const std::string& before, const std::string& after) {
    return CellDiff().diff(Snapshot("budget.csv", before), Snapshot("budget.csv", after));
}

bool hasChange(const DiffResult& result, const std::string& location, const std::string& before,
               const std::string& after) {
    for (const Change& change : result.changes) {
        if (change.location == location && change.before == before && change.after == after) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(parseRowsKeepsFieldsExactly) {
    const CSVHandler::Rows rows = CSVHandler::parseRows("\xEF\xBB\xBFName,Cost\r\nRent,1.50\r\n\"a,b\",007,\r\n");
    CHECK(rows.size() == 3);
    CHECK(rows[0] == (CSVHandler::Row{"Name", "Cost"}));
    CHECK(rows[1] == (CSVHandler::Row{"Rent", "1.50"}));
    CHECK(rows[2] == (CSVHandler::Row{"a,b", "007", ""}));
    CHECK(CSVHandler::parseRows("").empty());
}

TEST(formatRowsRoundTrips) {
    const std::string csv = "Name,Cost\nRent,1.50\n\"a,b\",\"say \"\"hi\"\"\"\n";
    CHECK(CSVHandler::formatRows(CSVHandler::parseRows(csv), "\n") == csv);
}

// Issue #32
TEST(cellDiffReportsAnEditedCell) {
    const DiffResult result = diffSheets("Item,Cost\nFood,10\nRent,40\n", "Item,Cost\nFood,10\nRent,55\n");
    CHECK(result.changes.size() == 1);
    CHECK(hasChange(result, "B3", "40", "55"));
}

TEST(cellDiffFindsNothingInIdenticalSheets) {
    CHECK(diffSheets("Item,Cost\nRent,40\n", "Item,Cost\r\nRent,40\r\n").changes.empty());
    CHECK(diffSheets("a,b\n", "a,b,\n").changes.empty());
}

TEST(cellDiffReportsAnInsertedRowOnce) {
    const DiffResult result =
        diffSheets("Item,Cost\nFood,10\nRent,40\n", "Item,Cost\nGym,30\nFood,10\nRent,40\n");
    CHECK(result.changes.size() == 1);
    CHECK(hasChange(result, "row 2", "", "Gym,30"));
}

TEST(cellDiffReportsARemovedRow) {
    const DiffResult result = diffSheets("Item,Cost\nFood,10\nRent,40\n", "Item,Cost\nRent,40\n");
    CHECK(result.changes.size() == 1);
    CHECK(hasChange(result, "row 2", "Food,10", ""));
}

TEST(cellDiffUsesNewRowNumbersAfterAnInsert) {
    const DiffResult result =
        diffSheets("Item,Cost\nFood,10\nRent,40\n", "Item,Cost\nGym,30\nFood,10\nRent,55\n");
    CHECK(result.changes.size() == 2);
    CHECK(hasChange(result, "row 2", "", "Gym,30"));
    CHECK(hasChange(result, "B4", "40", "55"));
}

TEST(cellDiffReportsFilledInAndClearedCells) {
    const DiffResult result = diffSheets("a,b\nc,d\n", "a,b,new\nc,\n");
    CHECK(result.changes.size() == 2);
    CHECK(hasChange(result, "C1", "", "new"));
    CHECK(hasChange(result, "B2", "d", ""));
}

TEST(cellDiffComparesFormulasAsWritten) {
    const DiffResult result = diffSheets("1,=A1*2\n", "1,=A1*3\n");
    CHECK(result.changes.size() == 1);
    CHECK(hasChange(result, "B1", "=A1*2", "=A1*3"));
}

// #57 review: the new row must not be paired with the edited row below it.
TEST(cellDiffPairsAnEditedRowWithItselfAfterAnInsertAboveIt) {
    const DiffResult result = diffSheets("Item,Cost\nRent,1000\nFood,300\nGym,0\n",
                                         "Item,Cost\nTravel,150\nRent,1100\nFood,300\nGym,0\n");
    CHECK(result.changes.size() == 2);
    CHECK(hasChange(result, "row 2", "", "Travel,150"));
    CHECK(hasChange(result, "B3", "1000", "1100"));
}

TEST(cellDiffDoesNotPairUnrelatedRows) {
    const DiffResult result = diffSheets("Item,Cost\nRent,1000\n", "Item,Cost\nTravel,150\n");
    CHECK(result.changes.size() == 2);
    CHECK(hasChange(result, "row 2", "Rent,1000", ""));
    CHECK(hasChange(result, "row 2", "", "Travel,150"));
}

TEST(cellDiffPairsEditedRowsByNameNotPosition) {
    const DiffResult result = diffSheets("Item,Cost\nRent,1000\nFood,300\n", "Item,Cost\nFood,350\nGym,40\n");
    CHECK(hasChange(result, "B2", "300", "350"));
    CHECK(hasChange(result, "row 2", "Rent,1000", ""));
    CHECK(hasChange(result, "row 3", "", "Gym,40"));
}

TEST(cellDiffHandlesNewAndDeletedFiles) {
    CHECK(diffSheets("", "a,b\n").changes.size() == 1);
    CHECK(hasChange(diffSheets("a,b\n", ""), "row 1", "a,b", ""));
}
