#include "TestHarness.h"

#include <string>

#include "core/Commit.h"

namespace {

Commit::Files files(const std::string& notes, const std::string& budget) {
    Commit::Files result;
    result.emplace("notes.txt", Snapshot("notes.txt", notes));
    result.emplace("budget.csv", Snapshot("budget.csv", budget));
    return result;
}

}  // namespace

// Issue #3
TEST(commitStoresItsDetails) {
    const Commit commit(2, 1, "Swaraj", "fixed totals", 1793015606, files("hello", "A1,10"));
    CHECK(commit.getId() == 2);
    CHECK(commit.getParentId() == 1);
    CHECK(commit.hasParent());
    CHECK(commit.getAuthor() == "Swaraj");
    CHECK(commit.getMessage() == "fixed totals");
    CHECK(commit.getTimestamp() == 1793015606);
    CHECK(commit.getFiles().size() == 2);
}

TEST(firstCommitHasNoParent) {
    const Commit commit(1, 0, "Swaraj", "initial", 0, {});
    CHECK(!commit.hasParent());
}

TEST(commitFindsFilesByPath) {
    const Commit commit(1, 0, "Swaraj", "initial", 0, files("hello", "A1,10"));
    const Snapshot* notes = commit.findFile("notes.txt");
    CHECK(notes != nullptr);
    CHECK(notes->getContent() == "hello");
    CHECK(commit.findFile("missing.txt") == nullptr);
}

TEST(commitComparesFileContents) {
    const Commit commit(1, 0, "Swaraj", "initial", 0, files("hello", "A1,10"));
    CHECK(commit.hasSameFiles(files("hello", "A1,10")));
    CHECK(!commit.hasSameFiles(files("hello!", "A1,10")));

    Commit::Files fewer;
    fewer.emplace("notes.txt", Snapshot("notes.txt", "hello"));
    CHECK(!commit.hasSameFiles(fewer));
}

TEST(commitSummaryNamesIdAuthorAndMessage) {
    const Commit commit(3, 2, "Swaraj", "fixed totals", 1793015606, {});
    const std::string summary = commit.summary();
    CHECK(summary.rfind("#3  ", 0) == 0);
    CHECK(summary.find("Swaraj") != std::string::npos);
    CHECK(summary.find("fixed totals") != std::string::npos);
    CHECK(commit.formattedTime().size() == 16);  // "YYYY-MM-DD HH:MM"
}
