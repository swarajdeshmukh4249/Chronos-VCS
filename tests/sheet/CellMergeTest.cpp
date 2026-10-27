#include "TestHarness.h"

#include <string>

#include "common/Snapshot.h"
#include "sheet/CSVHandler.h"
#include "sheet/CellMerge.h"

namespace {

MergeResult mergeSheets(const std::string& base, const std::string& ours, const std::string& theirs) {
    return CellMerge().merge(Snapshot("budget.csv", base), Snapshot("budget.csv", ours),
                             Snapshot("budget.csv", theirs));
}

const std::string kBase = "Name,Q1,Q2\nRent,1200,1200\nFood,400,400\n";

}  // namespace

// Issue #33: the integration checkpoint.
TEST(cellMergeCombinesEditsToDifferentCellsOfOneRow) {
    const MergeResult result = mergeSheets(kBase, "Name,Q1,Q2\nRent,1300,1200\nFood,400,400\n",
                                           "Name,Q1,Q2\nRent,1200,1250\nFood,400,400\n");
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == "Name,Q1,Q2\nRent,1300,1250\nFood,400,400\n");
}

TEST(cellMergeMarksTheSameCellChangedTwoWays) {
    const MergeResult result = mergeSheets(kBase, "Name,Q1,Q2\nRent,1200,1300\nFood,400,400\n",
                                           "Name,Q1,Q2\nRent,1200,1250\nFood,400,450\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].location == "C2");
    CHECK(result.conflicts[0].base == "1200");
    CHECK(result.conflicts[0].ours == "1300");
    CHECK(result.conflicts[0].theirs == "1250");
    CHECK(result.mergedContent ==
          "Name,Q1,Q2\nRent,1200,<<<<<<< ours: 1300 ======= theirs: 1250 >>>>>>>\nFood,400,450\n");
    // The rest of the sheet still reads as a normal CSV.
    const CSVHandler::Rows rows = CSVHandler::parseRows(result.mergedContent);
    CHECK(rows.size() == 3);
    CHECK(rows[2][2] == "450");
}

TEST(cellMergeTakesTheSameChangeOnce) {
    const std::string both = "Name,Q1,Q2\nRent,1300,1200\nFood,400,400\n";
    const MergeResult result = mergeSheets(kBase, both + "Gym,30,30\n", both);
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == both + "Gym,30,30\n");
}

TEST(cellMergeKeepsARowInsertedOnOneSideAndACellEditedOnTheOther) {
    const MergeResult result = mergeSheets(kBase, "Name,Q1,Q2\nGym,30,30\nRent,1200,1200\nFood,400,400\n",
                                           "Name,Q1,Q2\nRent,1200,1200\nFood,400,450\n");
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == "Name,Q1,Q2\nGym,30,30\nRent,1200,1200\nFood,400,450\n");
}

TEST(cellMergeKeepsRowsBothSidesAdded) {
    const MergeResult result =
        mergeSheets(kBase, kBase + "Gym,30,30\nBus,20,20\n", kBase + "Phone,50,50\nBus,20,20\n");
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == kBase + "Gym,30,30\nBus,20,20\nPhone,50,50\n");
}

TEST(cellMergeDropsARowDeletedOnOneSideAndUntouchedOnTheOther) {
    const MergeResult result =
        mergeSheets(kBase, "Name,Q1,Q2\nFood,400,400\n", "Name,Q1,Q2\nRent,1200,1200\nFood,400,450\n");
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == "Name,Q1,Q2\nFood,400,450\n");
}

TEST(cellMergeKeepsARowDeletedOnOneSideButEditedOnTheOther) {
    const MergeResult result =
        mergeSheets(kBase, "Name,Q1,Q2\nFood,400,400\n", "Name,Q1,Q2\nRent,1200,1250\nFood,400,400\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].location == "row 2");
    CHECK(result.conflicts[0].ours.empty());
    CHECK(result.conflicts[0].theirs == "Rent,1200,1250");
    CHECK(result.mergedContent == "Name,Q1,Q2\nRent,1200,1250\nFood,400,400\n");
}

TEST(cellMergeLeavesUntouchedCellsExactly) {
    const std::string base = "Name,Cost,Code\nRent,1.50,007\nFood,2.00,\"a,b\"\n";
    const MergeResult result = mergeSheets(base, "Name,Cost,Code\nRent,1.75,007\nFood,2.00,\"a,b\"\n",
                                           "Name,Cost,Code\nRent,1.50,007\nFood,2.00,\"a,c\"\n");
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == "Name,Cost,Code\nRent,1.75,007\nFood,2.00,\"a,c\"\n");
}

TEST(cellMergeKeepsOurLineEndingsAndByteOrderMark) {
    const std::string bom = "\xEF\xBB\xBF";
    const MergeResult result = mergeSheets(bom + "a,b\r\nc,d\r\n", bom + "A,b\r\nc,d\r\n", "a,b\nc,D\n");
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == bom + "A,b\r\nc,D\r\n");
}

TEST(cellMergeReturnsTheChangedSideWhenOnlyOneChanged) {
    const std::string ours = "Name,Q1,Q2\r\nRent,1300,1200\r\n";
    CHECK(mergeSheets(kBase, ours, kBase).mergedContent == ours);
    CHECK(mergeSheets(kBase, kBase, ours).mergedContent == ours);
}

// #57 review: inserting a row above an edited row must not move the conflict.
TEST(cellMergeFindsTheConflictOnTheEditedRowAfterAnInsertAboveIt) {
    const std::string base = "Item,Cost\nRent,1000\nFood,300\nGym,0\n";
    const MergeResult result = mergeSheets(base, "Item,Cost\nTravel,150\nRent,1100\nFood,300\nGym,0\n",
                                           "Item,Cost\nRent,1200\nFood,300\nGym,0\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].location == "B3");
    CHECK(result.conflicts[0].ours == "1100");
    CHECK(result.conflicts[0].theirs == "1200");
    CHECK(result.mergedContent ==
          "Item,Cost\nTravel,150\nRent,<<<<<<< ours: 1100 ======= theirs: 1200 >>>>>>>\nFood,300\nGym,0\n");
}

TEST(cellMergeCombinesAnInsertAboveWithAnEditToTheSameRow) {
    const std::string base = "Item,Cost,Paid\nRent,1000,no\n";
    const MergeResult result =
        mergeSheets(base, "Item,Cost,Paid\nTravel,150,no\nRent,1100,no\n", "Item,Cost,Paid\nRent,1000,yes\n");
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == "Item,Cost,Paid\nTravel,150,no\nRent,1100,yes\n");
}

TEST(cellMergeFlagsRowsBothSidesAddedForTheSameItem) {
    const MergeResult result = mergeSheets(kBase, kBase + "Travel,150,150\n", kBase + "Travel,200,200\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].location == "row 5");
    CHECK(result.conflicts[0].ours == "Travel,150,150");
    CHECK(result.conflicts[0].theirs == "Travel,200,200");
    CHECK(result.mergedContent == kBase + "Travel,150,150\nTravel,200,200\n");
}

TEST(cellMergeShowsEmptyValuesInConflictCells) {
    const MergeResult result = mergeSheets("a,b\n", "a,\n", "a,c\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(result.mergedContent == "a,<<<<<<< ours: (empty) ======= theirs: c >>>>>>>\n");
}
