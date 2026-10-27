#include "TestHarness.h"

#include <string>

#include "TempDir.h"
#include "common/Exceptions.h"
#include "core/Repository.h"

namespace {

void writeFile(const TempDir& dir, const std::string& path, const std::string& content) {
    WorkingTree(dir.path()).write(path, content);
}

std::string readFile(const TempDir& dir, const std::string& path) {
    return WorkingTree(dir.path()).read(path);
}

}  // namespace

// Issue #4
TEST(repositoryCommitsAndLogsInOrder) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "version 1");
    repo.track("notes.txt");
    repo.commit("first", "Swaraj");
    writeFile(dir, "notes.txt", "version 2");
    repo.commit("second", "Swaraj");
    writeFile(dir, "notes.txt", "version 3");
    const Commit& third = repo.commit("third", "Tanishka");

    CHECK(third.getId() == 3);
    CHECK(third.getParentId() == 2);
    CHECK(third.getAuthor() == "Tanishka");
    CHECK(repo.log().size() == 3);
    CHECK(repo.log().at(1).getMessage() == "first");
    CHECK(repo.log().at(1).findFile("notes.txt")->getContent() == "version 1");
    CHECK(repo.log().at(3).findFile("notes.txt")->getContent() == "version 3");
}

TEST(repositoryKeepsHistoryAfterReopening) {
    TempDir dir;
    {
        Repository repo = Repository::init(dir.path());
        writeFile(dir, "notes.txt", "hello");
        repo.track("notes.txt");
        repo.commit("first", "Swaraj");
    }
    const Repository reopened = Repository::open(dir.path());
    CHECK(reopened.log().size() == 1);
    CHECK(reopened.getCommit(1).getMessage() == "first");
    CHECK(reopened.getTracked().count("notes.txt") == 1);
}

TEST(repositoryRefusesEmptyCommits) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    CHECK_THROWS(repo.commit("nothing tracked", "Swaraj"), NothingToCommitException);

    writeFile(dir, "notes.txt", "hello");
    repo.track("notes.txt");
    CHECK_THROWS(repo.commit("", "Swaraj"), ChronoException);
    repo.commit("first", "Swaraj");
    CHECK_THROWS(repo.commit("no changes", "Swaraj"), NothingToCommitException);
    CHECK(repo.log().size() == 1);
}

TEST(repositoryRecordsDeletedFiles) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "a.txt", "a");
    writeFile(dir, "b.txt", "b");
    repo.track("a.txt");
    repo.track("b.txt");
    repo.commit("both", "Swaraj");
    WorkingTree(dir.path()).remove("b.txt");
    const Commit& second = repo.commit("removed b", "Swaraj");
    CHECK(second.findFile("a.txt") != nullptr);
    CHECK(second.findFile("b.txt") == nullptr);
}

TEST(repositoryRejectsUnknownFilesAndCommits) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    CHECK_THROWS(repo.track("missing.txt"), FileNotFoundException);
    CHECK_THROWS(repo.getCommit(1), CommitNotFoundException);
    CHECK_THROWS(repo.getCommit(0), CommitNotFoundException);
    CHECK_THROWS(repo.getCommit(-1), CommitNotFoundException);
    CHECK_THROWS(Repository::init(dir.path()), RepositoryExistsException);
}

TEST(repositoryTracksFilesInSubfolders) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "data/budget.csv", "A1,10\n");
    CHECK(repo.track(dir.path() / "data" / "budget.csv") == "data/budget.csv");
    repo.commit("budget", "Swaraj");
    CHECK(repo.getCommit(1).findFile("data/budget.csv")->getContent() == readFile(dir, "data/budget.csv"));
}

// Issue #6
TEST(checkoutPutsOldFilesBack) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "version 1");
    repo.track("notes.txt");
    repo.commit("first", "Swaraj");
    writeFile(dir, "notes.txt", "version 2");
    writeFile(dir, "extra.txt", "added later");
    repo.track("extra.txt");
    repo.commit("second", "Swaraj");

    repo.checkout(1);
    CHECK(readFile(dir, "notes.txt") == "version 1");
    CHECK(!WorkingTree(dir.path()).exists("extra.txt"));
    CHECK(repo.log().size() == 2);

    repo.checkout(2);
    CHECK(readFile(dir, "notes.txt") == "version 2");
    CHECK(readFile(dir, "extra.txt") == "added later");
}

TEST(checkoutProtectsUncommittedWork) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "version 1");
    repo.track("notes.txt");
    repo.commit("first", "Swaraj");
    writeFile(dir, "notes.txt", "unsaved edit");
    CHECK(repo.hasUncommittedChanges());
    CHECK_THROWS(repo.checkout(1), UncommittedChangesException);
    CHECK(readFile(dir, "notes.txt") == "unsaved edit");
    CHECK_THROWS(repo.checkout(9), CommitNotFoundException);
}

TEST(revertRecordsANewCommit) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "version 1");
    repo.track("notes.txt");
    repo.commit("first", "Swaraj");
    writeFile(dir, "notes.txt", "version 2");
    repo.commit("second", "Swaraj");
    writeFile(dir, "notes.txt", "version 3");
    repo.commit("third", "Swaraj");

    const Commit& reverted = repo.revert(1, "Swaraj");
    CHECK(reverted.getId() == 4);
    CHECK(reverted.getParentId() == 3);
    CHECK(reverted.getMessage() == "Revert to #1: first");
    CHECK(readFile(dir, "notes.txt") == "version 1");
    CHECK(!repo.hasUncommittedChanges());
    CHECK_THROWS(repo.revert(4, "Swaraj"), NothingToCommitException);
}

TEST(headIsRememberedBetweenRuns) {
    TempDir dir;
    {
        Repository repo = Repository::init(dir.path());
        CHECK(repo.getHead() == 0);
        writeFile(dir, "notes.txt", "version 1");
        repo.track("notes.txt");
        repo.commit("first", "Swaraj");
        writeFile(dir, "notes.txt", "version 2");
        repo.commit("second", "Swaraj");
        repo.checkout(1);
    }
    Repository reopened = Repository::open(dir.path());
    CHECK(reopened.getHead() == 1);
    CHECK(!reopened.hasUncommittedChanges());
    reopened.checkout(2);
    CHECK(readFile(dir, "notes.txt") == "version 2");
}

TEST(commitAfterCheckoutUsesHeadAsParent) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "version 1");
    repo.track("notes.txt");
    repo.commit("first", "Swaraj");
    writeFile(dir, "notes.txt", "version 2");
    repo.commit("second", "Swaraj");
    repo.checkout(1);
    writeFile(dir, "notes.txt", "another idea");
    const Commit& third = repo.commit("try another idea", "Swaraj");
    CHECK(third.getId() == 3);
    CHECK(third.getParentId() == 1);
    CHECK(repo.getHead() == 3);
}

// Issue #11
TEST(diffBetweenCommitsListsChangedFiles) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "keep.txt", "same\n");
    writeFile(dir, "notes.txt", "Budget v1\n");
    writeFile(dir, "old.txt", "bye\n");
    repo.track("keep.txt");
    repo.track("notes.txt");
    repo.track("old.txt");
    repo.commit("first", "Swaraj");
    writeFile(dir, "notes.txt", "Budget v2\n");
    WorkingTree(dir.path()).remove("old.txt");
    writeFile(dir, "new.txt", "hello\n");
    repo.track("new.txt");
    repo.commit("second", "Swaraj");

    const std::vector<FileDiff> diffs = repo.diff(1, 2);
    CHECK(diffs.size() == 3);  // keep.txt is unchanged, so it is left out
    CHECK(diffs[0].path == "new.txt");
    CHECK(diffs[0].status == FileDiff::Status::Added);
    CHECK(diffs[0].result.changes[0].after == "hello\n");
    CHECK(diffs[1].path == "notes.txt");
    CHECK(diffs[1].status == FileDiff::Status::Modified);
    CHECK(diffs[1].result.changes[0].before == "Budget v1\n");
    CHECK(diffs[1].result.changes[0].after == "Budget v2\n");
    CHECK(diffs[2].path == "old.txt");
    CHECK(diffs[2].status == FileDiff::Status::Removed);

    CHECK(repo.diff(2, 2).empty());
    CHECK_THROWS(repo.diff(1, 7), CommitNotFoundException);
}

TEST(diffUncommittedComparesWithHead) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "a\nb\n");
    repo.track("notes.txt");
    CHECK(repo.diffUncommitted().size() == 1);  // nothing committed yet, so the file is new
    repo.commit("first", "Swaraj");
    CHECK(repo.diffUncommitted().empty());
    writeFile(dir, "notes.txt", "a\nB\n");
    const std::vector<FileDiff> diffs = repo.diffUncommitted();
    CHECK(diffs.size() == 1);
    CHECK(diffs[0].result.changes[0].location == "line 2");
}

// Issue #8
TEST(newRepositoryStartsOnMain) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    CHECK(repo.getCurrentBranch() == "main");
    CHECK(repo.getBranches().size() == 1);
    CHECK(repo.getBranches()[0].getCommitId() == 0);
}

TEST(twoBranchesHoldDifferentContent) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "budget.txt", "Food 300\n");
    repo.track("budget.txt");
    repo.commit("first", "Swaraj");

    repo.createBranch("feature");
    CHECK(repo.getCurrentBranch() == "main");  // creating doesn't switch
    repo.switchBranch("feature");
    writeFile(dir, "budget.txt", "Food 350\n");
    const int featureCommit = repo.commit("raise food", "Tanishka").getId();

    repo.switchBranch("main");
    CHECK(readFile(dir, "budget.txt") == "Food 300\n");
    writeFile(dir, "budget.txt", "Food 300\nGym 40\n");
    const int mainCommit = repo.commit("add gym", "Swaraj").getId();

    repo.switchBranch("feature");
    CHECK(readFile(dir, "budget.txt") == "Food 350\n");
    CHECK(repo.getHead() == featureCommit);

    // Both branches grew from commit 1.
    CHECK(repo.getCommit(featureCommit).getParentId() == 1);
    CHECK(repo.getCommit(mainCommit).getParentId() == 1);
    CHECK(repo.historyOfHead() == (std::vector<int>{featureCommit, 1}));
}

TEST(branchesAreRememberedBetweenRuns) {
    TempDir dir;
    {
        Repository repo = Repository::init(dir.path());
        writeFile(dir, "notes.txt", "v1\n");
        repo.track("notes.txt");
        repo.commit("first", "Swaraj");
        repo.createBranch("feature");
        repo.switchBranch("feature");
        writeFile(dir, "notes.txt", "v2\n");
        repo.commit("second", "Swaraj");
    }
    const Repository reopened = Repository::open(dir.path());
    CHECK(reopened.getCurrentBranch() == "feature");
    CHECK(reopened.getHead() == 2);
    CHECK(reopened.getBranches().size() == 2);
    CHECK(reopened.getBranches()[1].getName() == "main");
    CHECK(reopened.getBranches()[1].getCommitId() == 1);
}

TEST(branchRulesAreEnforced) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "v1\n");
    repo.track("notes.txt");
    repo.commit("first", "Swaraj");

    CHECK_THROWS(repo.createBranch("main"), BranchExistsException);
    CHECK_THROWS(repo.createBranch("Main"), BranchExistsException);
    CHECK_THROWS(repo.createBranch("bad name"), InvalidBranchNameException);
    CHECK_THROWS(repo.switchBranch("nope"), BranchNotFoundException);

    // Names ignore case everywhere, so "Main" finds main and HEAD keeps the stored spelling.
    repo.switchBranch("Main");
    CHECK(repo.getCurrentBranch() == "main");
    CHECK(Repository::open(dir.path()).getCurrentBranch() == "main");

    repo.createBranch("feature");
    writeFile(dir, "notes.txt", "unsaved\n");
    CHECK_THROWS(repo.switchBranch("feature"), UncommittedChangesException);
    CHECK(readFile(dir, "notes.txt") == "unsaved\n");
}

TEST(checkoutLeavesTheBranch) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "v1\n");
    repo.track("notes.txt");
    repo.commit("first", "Swaraj");
    writeFile(dir, "notes.txt", "v2\n");
    repo.commit("second", "Swaraj");

    repo.checkout(1);
    CHECK(repo.getCurrentBranch().empty());
    writeFile(dir, "notes.txt", "experiment\n");
    repo.commit("experiment", "Swaraj");
    // main didn't move, because HEAD wasn't on it.
    CHECK(repo.getBranches()[0].getCommitId() == 2);

    repo.switchBranch("main");
    CHECK(readFile(dir, "notes.txt") == "v2\n");
    CHECK(repo.getCurrentBranch() == "main");
}
