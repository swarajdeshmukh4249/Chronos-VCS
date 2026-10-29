#include "TestHarness.h"

#include <string>

#include "common/Snapshot.h"
#include "sheet/DependencyAwareMerge.h"
#include "sheet/MergeImpactReport.h"

namespace {

MergeResult mergeSheets(const std::string& base, const std::string& ours, const std::string& theirs) {
    return DependencyAwareMerge().merge(Snapshot("budget.csv", base), Snapshot("budget.csv", ours),
                                        Snapshot("budget.csv", theirs));
}

const std::string kOverBudget = "10,,\"=IF(A1>20,\"\"OVER BUDGET\"\",A1*2)\"\n";

}  // namespace

// Issue #34: the example the project is pitched on.
TEST(aFormulaChangedOnOneSideReadingACellChangedOnTheOtherIsFlagged) {
    const MergeResult result = mergeSheets("10,,=A1*2\n", "50,,=A1*2\n", kOverBudget);
    CHECK(result.conflicts.size() == 1);
    const Conflict& conflict = result.conflicts[0];
    CHECK(conflict.semantic);
    CHECK(conflict.location == "C1");
    CHECK(conflict.base == "20");
    CHECK(conflict.ours == "100");
    CHECK(conflict.theirs == "20");
    CHECK(conflict.explanation.find("C1 (changed on the other branch) reads A1 (changed on this branch)") !=
          std::string::npos);
    CHECK(conflict.explanation.find("OVER BUDGET") != std::string::npos);
    // The cells still merged: the file has both edits and no markers.
    CHECK(result.mergedContent.find("50,,") == 0);
    CHECK(result.mergedContent.find("<<<<<<<") == std::string::npos);
}

TEST(theSidesSwappedAreFlaggedTheOtherWayRound) {
    const MergeResult result = mergeSheets("10,,=A1*2\n", kOverBudget, "50,,=A1*2\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].explanation.find("C1 (changed on this branch) reads A1 (changed on the other branch)") !=
          std::string::npos);
}

TEST(aChangeReachingTheFormulaThroughAnotherCellIsFlagged) {
    // B1 reads A1, C1 reads B1. Ours changes A1; theirs changes C1's formula.
    const MergeResult result = mergeSheets("10,=A1+1,=B1*2\n", "50,=A1+1,=B1*2\n", "10,=A1+1,=B1*3\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].location == "C1");
    CHECK(result.conflicts[0].ours == "102");
    CHECK(result.conflicts[0].theirs == "33");
}

TEST(aCellInsideARangeTheOtherSideChangedIsFlagged) {
    // Theirs turns the total into an average; ours fills in a cell that was empty.
    const std::string base = "1,=SUM(A1:A3)\n2\n\n";
    const MergeResult result = mergeSheets(base, "1,=SUM(A1:A3)\n2\n9\n", "1,=AVG(A1:A3)\n2\n\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].location == "B1");
    CHECK(result.conflicts[0].explanation.find("reads A3") != std::string::npos);
}

TEST(onlyInputsChangedIsAnOrdinaryMerge) {
    // Both sides change numbers the total reads, but nobody changed the formula,
    // so the new total is what anyone would expect.
    const MergeResult result = mergeSheets("1,=A1+A2\n2\n", "5,=A1+A2\n2\n", "1,=A1+A2\n7\n");
    CHECK(result.conflicts.empty());
}

TEST(aFormulaChangeNoChangedCellFeedsIsAnOrdinaryMerge) {
    const MergeResult result = mergeSheets("10,1,=A1*2\n", "10,5,=A1*2\n", "10,1,=A1*3\n");
    CHECK(result.conflicts.empty());
}

TEST(cellConflictsComeFirstAndNoFormulaCheckRuns) {
    const MergeResult result = mergeSheets("Rent,10,=B1*2\n", "Rent,50,=B1*2\n", "Rent,70,=B1*3\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(!result.conflicts[0].semantic);
    CHECK(result.conflicts[0].location == "B1");
}

// Issue #35
TEST(theImpactReportShowsValuesBeforeAndAfterTheMerge) {
    const std::string merged = "50,,\"=IF(A1>20,\"\"OVER BUDGET\"\",A1*2)\"\n";
    const MergeImpactReport report("10,,=A1*2\n", "50,,=A1*2\n", kOverBudget, merged);
    CHECK(report.impacts().size() == 1);
    const CellImpact& impact = report.impacts()[0];
    CHECK(impact.base == "20");
    CHECK(impact.ours == "100");
    CHECK(impact.theirs == "20");
    CHECK(impact.merged == "OVER BUDGET");
    CHECK(report.lines().size() == 1);
    CHECK(report.lines()[0] ==
          "C1: before 20, this branch 100, the other branch 20, merged OVER BUDGET  (semantic conflict)");
}

TEST(theImpactReportListsOrdinaryChangesToo) {
    const MergeImpactReport report("1,=A1+A2\n2\n", "5,=A1+A2\n2\n", "1,=A1+A2\n7\n", "5,=A1+A2\n7\n");
    CHECK(report.impacts().size() == 1);
    CHECK(!report.impacts()[0].semantic);
    CHECK(report.impacts()[0].merged == "12");
    CHECK(report.semanticConflicts().empty());
}

TEST(aSheetWithoutFormulasHasNoImpacts) {
    CHECK(MergeImpactReport("a,1\n", "a,2\n", "b,1\n", "b,2\n").impacts().empty());
}

// The paradox in C1 also breaks the total in C3, which reads it.
TEST(aParadoxNamesTheCellsItKnocksOn) {
    const std::string base = "Rent,10,=B1*2\nFood,30,=B2*2\nSum,,=C1+C2\n";
    const std::string ours = "Rent,50,=B1*2\nFood,30,=B2*2\nSum,,=C1+C2\n";
    const std::string theirs = "Rent,10,\"=IF(B1>20,\"\"OVER BUDGET\"\",B1*2)\"\nFood,30,=B2*2\nSum,,=C1+C2\n";
    const MergeResult result = mergeSheets(base, ours, theirs);
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].location == "C1");
    CHECK(result.conflicts[0].explanation.find("Because of it, C3 (now an error) changes too.") != std::string::npos);
}
