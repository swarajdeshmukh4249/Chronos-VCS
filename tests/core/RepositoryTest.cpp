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
