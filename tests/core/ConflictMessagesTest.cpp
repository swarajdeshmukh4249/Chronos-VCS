#include "TestHarness.h"

#include <algorithm>
#include <string>
#include <vector>

#include "core/ConflictMessages.h"

namespace {

Conflict cellClash() {
    Conflict conflict;
    conflict.location = "C2";
    conflict.base = "1200";
    conflict.ours = "1300";
    conflict.theirs = "1250";
    conflict.explanation =
        "Both versions changed C2 in different ways: this branch to 1300, the other branch to 1250 (it was 1200).";
    return conflict;
}

Conflict lineClash() {
    Conflict conflict;
    conflict.location = "lines 2-3";
    conflict.base = "old one\nold two\n";
    conflict.ours = "our one\nour two\n";
    conflict.theirs = "their one\n";
    conflict.explanation = "Both versions changed lines 2-3 in different ways.";
    return conflict;
}

MergeOutcome outcomeWith(std::vector<MergeOutcome::FileConflict> conflicts) {
    MergeOutcome outcome;
    outcome.kind = MergeOutcome::Kind::Conflicts;
    outcome.conflicts = std::move(conflicts);
    return outcome;
}

bool has(const std::vector<std::string>& lines, const std::string& text) {
    return std::any_of(lines.begin(), lines.end(),
                       [&text](const std::string& line) { return line.find(text) != std::string::npos; });
}

bool hasExactly(const std::vector<std::string>& lines, const std::string& text) {
    return std::find(lines.begin(), lines.end(), text) != lines.end();
}

}  // namespace

// Issue #19
TEST(aCellConflictNamesTheCellAndBothBranches) {
    const ConflictMessage message = ConflictMessages::explain("budget.csv", cellClash(), "main", "experiment");
    CHECK(message.headline == "budget.csv, cell C2");
    // The merge's own sentence is used as it stands: it is the authority on
    // what happened.
    CHECK(message.details.front() == cellClash().explanation);
    CHECK(hasExactly(message.details, "on main: 1300"));
    CHECK(hasExactly(message.details, "on experiment: 1250"));
    CHECK(hasExactly(message.details, "before: 1200"));
    CHECK(message.advice.find("Replace the whole cell") != std::string::npos);
}

TEST(aTextConflictTalksAboutLinesNotCells) {
    const ConflictMessage message = ConflictMessages::explain("notes.txt", lineClash(), "main", "experiment");
    CHECK(message.headline == "notes.txt, lines 2-3");
    CHECK(message.advice.find("Keep the lines you want") != std::string::npos);
    CHECK(message.advice.find("cell") == std::string::npos);
}

TEST(contentOfMoreThanOneLineGetsLinesOfItsOwn) {
    const ConflictMessage message = ConflictMessages::explain("notes.txt", lineClash(), "main", "experiment");
    // Two lines can't sit next to a label, so the label stands alone and the
    // lines are indented under it.
    CHECK(hasExactly(message.details, "on main:"));
    CHECK(has(message.details, "our one"));
    CHECK(has(message.details, "our two"));
    // One line still sits next to its label.
    CHECK(hasExactly(message.details, "on experiment: their one"));
}

TEST(onlyACellReferenceIsCalledACell) {
    // A sheet's row conflicts are rows, not cells.
    Conflict row;
    row.location = "row 4";
    row.ours = "Rent,1300";
    row.theirs = "Rent,1250";
    CHECK(ConflictMessages::explain("budget.csv", row, "main", "experiment").headline == "budget.csv, row 4");

    // And a cell reference in a file that isn't a sheet is left alone.
    CHECK(ConflictMessages::explain("notes.txt", cellClash(), "main", "experiment").headline == "notes.txt, C2");

    Conflict wide = cellClash();
    wide.location = "AB13";
    CHECK(ConflictMessages::explain("budget.CSV", wide, "main", "experiment").headline == "budget.CSV, cell AB13");
}

TEST(anEmptySideIsCalledNothingRatherThanLeftBlank) {
    Conflict deleted;
    deleted.location = "row 3";
    deleted.base = "Rent,1200";
    deleted.ours = "";
    deleted.theirs = "Rent,1250";
    deleted.explanation = "This branch deleted row 3 and the other branch changed it.";

    const ConflictMessage message = ConflictMessages::explain("budget.csv", deleted, "main", "experiment");
    CHECK(hasExactly(message.details, "on main: nothing"));
    CHECK(hasExactly(message.details, "on experiment: Rent,1250"));
    CHECK(hasExactly(message.details, "before: Rent,1200"));
}

TEST(aPureAdditionHasNoBeforeLine) {
    Conflict added;
    added.location = "after line 5";
    added.ours = "ours\n";
    added.theirs = "theirs\n";
    added.explanation = "Both versions added different lines after line 5.";

    const ConflictMessage message = ConflictMessages::explain("notes.txt", added, "main", "experiment");
    CHECK(!has(message.details, "before"));
}

TEST(aDetachedHeadIsCalledThisCommit) {
    // After `chrono checkout <id>` there is no current branch to name.
    const ConflictMessage message = ConflictMessages::explain("budget.csv", cellClash(), "", "experiment");
    CHECK(hasExactly(message.details, "on this commit: 1300"));
}

TEST(theReportSaysWhichBranchTheExplanationsMean) {
    // Without this, "this branch" in the merge's sentences is guesswork: a
    // MergeStrategy never learns the branch names, so only the report can say.
    const std::vector<std::string> lines =
        ConflictMessages::report(outcomeWith({{"budget.csv", cellClash()}}), "main", "experiment");
    CHECK(lines.front() == "Merge stopped: 1 conflict to fix.");
    CHECK(has(lines, "\"this branch\" is main and \"the other branch\" is experiment"));
}

TEST(theReportCountsConflictsAndEndsWithWhatToDo) {
    const std::vector<std::string> one =
        ConflictMessages::report(outcomeWith({{"budget.csv", cellClash()}}), "main", "experiment");
    CHECK(one.front() == "Merge stopped: 1 conflict to fix.");

    const std::vector<std::string> two = ConflictMessages::report(
        outcomeWith({{"budget.csv", cellClash()}, {"notes.txt", lineClash()}}), "main", "experiment");
    CHECK(two.front() == "Merge stopped: 2 conflicts to fix.");
    CHECK(has(two, "chrono commit -m"));
    CHECK(has(two, "chrono merge --abort"));
}

TEST(theReportCoversEveryConflictAndAdvisesOnEach) {
    const std::vector<std::string> lines = ConflictMessages::report(
        outcomeWith({{"budget.csv", cellClash()}, {"notes.txt", lineClash()}}), "main", "experiment");

    CHECK(hasExactly(lines, "budget.csv, cell C2"));
    CHECK(hasExactly(lines, "notes.txt, lines 2-3"));
    // A sheet and a text file need different advice, and both are present.
    CHECK(has(lines, "Replace the whole cell"));
    CHECK(has(lines, "Keep the lines you want"));
    // Every line is printable as it stands: no stray newlines inside one.
    CHECK(std::none_of(lines.begin(), lines.end(),
                       [](const std::string& line) { return line.find('\n') != std::string::npos; }));
}

TEST(aConflictWithNoExplanationStillReads) {
    // Nothing in Conflict is required, so a strategy that leaves the sentence
    // out must not produce a blank line in the middle of the message.
    Conflict bare;
    bare.location = "C2";
    bare.ours = "1300";
    bare.theirs = "1250";

    const ConflictMessage message = ConflictMessages::explain("budget.csv", bare, "main", "experiment");
    CHECK(!message.details.empty());
    CHECK(message.details.front() == "on main: 1300");
}
