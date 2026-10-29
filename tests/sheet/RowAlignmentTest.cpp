#include "TestHarness.h"

#include <cstddef>
#include <string>
#include <vector>

#include "sheet/RowAlignment.h"

namespace {

// True when old row i and new row j were taken for the same row, edited or not.
bool paired(const std::vector<RowMatch>& matches, std::size_t oldRow, std::size_t newRow) {
    for (const RowMatch& match : matches) {
        if (match.oldRow == oldRow && match.newRow == newRow) {
            return true;
        }
    }
    return false;
}

bool removedOnly(const std::vector<RowMatch>& matches, std::size_t oldRow) {
    for (const RowMatch& match : matches) {
        if (match.oldRow == oldRow) {
            return match.newRow == RowMatch::kNone;
        }
    }
    return false;
}

bool addedOnly(const std::vector<RowMatch>& matches, std::size_t newRow) {
    for (const RowMatch& match : matches) {
        if (match.newRow == newRow) {
            return match.oldRow == RowMatch::kNone;
        }
    }
    return false;
}

}  // namespace

// A row whose spare column is empty on both sides is still recognised as the
// same row. Counting that empty column made one match out of two real columns
// look like one out of three, so the row was read as deleted and re-added, and
// a merge then put it in the file twice.
TEST(aRowWithASpareEmptyColumnIsStillTheSameRow) {
    const CSVHandler::Rows before = {{"10", "", "=A1*2"}};
    const CSVHandler::Rows after = {{"50", "", "=A1*2"}};

    const std::vector<RowMatch> matches = alignRows(before, after);
    CHECK(matches.size() == 1);
    CHECK(paired(matches, 0, 0));
}

TEST(theSameRowIsFoundHoweverManySpareColumnsThereAre) {
    // Only the first and the last column hold anything, and the last matches.
    const CSVHandler::Rows before = {{"a", "", "", "", "", "keep"}};
    const CSVHandler::Rows after = {{"b", "", "", "", "", "keep"}};
    CHECK(paired(alignRows(before, after), 0, 0));
}

TEST(aRowKeepingItsNameIsTheSameRowWhateverElseChanged) {
    // The first field is the row's name, so it outweighs everything else.
    const CSVHandler::Rows before = {{"Rent", "1200", "=B1*2"}};
    const CSVHandler::Rows after = {{"Rent", "50", "=IF(B1>20,\"OVER BUDGET\",B1*2)"}};
    CHECK(paired(alignRows(before, after), 0, 0));
}

TEST(halfTheFieldsMatchingIsEnoughAndLessIsNot) {
    const CSVHandler::Rows before = {{"1", "2", "3"}};
    // Two of three match, which is over half.
    CHECK(paired(alignRows(before, {{"9", "2", "3"}}), 0, 0));
    // One of three is under half, so these count as different rows. Empty
    // columns are left out of that sum, but columns holding something are not.
    CHECK(!paired(alignRows(before, {{"9", "8", "3"}}), 0, 0));
}

TEST(rowsWithNothingInCommonAreNotTakenForEachOther) {
    // The guard that matters: pairing unrelated rows would merge two different
    // rows into one hybrid that neither side ever had.
    const CSVHandler::Rows before = {{"Rent", "1200", "monthly"}};
    const CSVHandler::Rows after = {{"Travel", "99", "yearly"}};

    const std::vector<RowMatch> matches = alignRows(before, after);
    CHECK(!paired(matches, 0, 0));
    CHECK(removedOnly(matches, 0));
    CHECK(addedOnly(matches, 0));
}

TEST(aDifferentlyNamedRowSharingOneFieldOfManyIsNotTheSameRow) {
    const CSVHandler::Rows before = {{"Rent", "1200", "monthly", "paid", "cash"}};
    const CSVHandler::Rows after = {{"Travel", "99", "yearly", "due", "cash"}};
    CHECK(!paired(alignRows(before, after), 0, 0));
}

TEST(unchangedRowsPairUpWithoutBeingCompared) {
    const CSVHandler::Rows rows = {{"Name", "Q1"}, {"Rent", "1200"}, {"Food", "400"}};
    const std::vector<RowMatch> matches = alignRows(rows, rows);
    CHECK(matches.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(paired(matches, i, i));
    }
}

TEST(anInsertedRowIsOneAdditionAndNothingElseMoves) {
    const CSVHandler::Rows before = {{"Name", "Q1"}, {"Rent", "1200"}, {"Food", "400"}};
    const CSVHandler::Rows after = {{"Name", "Q1"}, {"Rent", "1200"}, {"Travel", "99"}, {"Food", "400"}};

    const std::vector<RowMatch> matches = alignRows(before, after);
    CHECK(paired(matches, 0, 0));
    CHECK(paired(matches, 1, 1));
    CHECK(addedOnly(matches, 2));
    // Food moved down a line but is the same row, not a change.
    CHECK(paired(matches, 2, 3));
}

TEST(aDeletedRowIsOneRemoval) {
    const CSVHandler::Rows before = {{"Name", "Q1"}, {"Rent", "1200"}, {"Food", "400"}};
    const CSVHandler::Rows after = {{"Name", "Q1"}, {"Food", "400"}};

    const std::vector<RowMatch> matches = alignRows(before, after);
    CHECK(paired(matches, 0, 0));
    CHECK(removedOnly(matches, 1));
    CHECK(paired(matches, 2, 1));
}

TEST(anEditedRowIsPairedEvenWithAnInsertAboveIt) {
    // Travel is new and Rent was edited, so Rent must pair with Rent rather
    // than with the row that happens to sit in its old place.
    const CSVHandler::Rows before = {{"Name", "Q1"}, {"Rent", "1200"}};
    const CSVHandler::Rows after = {{"Name", "Q1"}, {"Travel", "99"}, {"Rent", "1300"}};

    const std::vector<RowMatch> matches = alignRows(before, after);
    CHECK(addedOnly(matches, 1));
    CHECK(paired(matches, 1, 2));
}

TEST(sameRowIgnoresEmptyFieldsOnTheEnd) {
    CHECK(sameRow({"a", "b"}, {"a", "b", ""}));
    CHECK(sameRow({"a", "b", "", ""}, {"a", "b"}));
    CHECK(!sameRow({"a", "b"}, {"a", "c"}));
    CHECK(!sameRow({"a", "b"}, {"a", "b", "c"}));
}

TEST(fieldAtReadsPastTheEndOfAShortRow) {
    const CSVHandler::Row row = {"a", "b"};
    CHECK(fieldAt(row, 0) == "a");
    CHECK(fieldAt(row, 1) == "b");
    CHECK(fieldAt(row, 2).empty());
    CHECK(fieldAt(row, 99).empty());
}
