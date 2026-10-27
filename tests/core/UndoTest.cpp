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

// Takes a snapshot, runs change, and remembers the snapshot, as Command::run does.
template <typename Change>
void undoable(Repository& repo, const std::string& description, Change change) {
    const RepositoryState before = repo.captureState(description);
    change();
    repo.rememberForUndo(before);
}

}  // namespace

// Issue #14
TEST(undoTakesBackACommitButKeepsTheEdits) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "v1\n");
    repo.track("notes.txt");
    repo.commit("first", "Swaraj");
    writeFile(dir, "notes.txt", "v2\n");
    undoable(repo, "commit \"second\"", [&] { repo.commit("second", "Swaraj"); });

    CHECK(repo.nextUndo() == "commit \"second\"");
    CHECK(repo.undo() == "commit \"second\"");
    CHECK(repo.getHead() == 1);
    CHECK(repo.getBranches()[0].getCommitId() == 1);
    CHECK(readFile(dir, "notes.txt") == "v2\n");  // the edit is back to being uncommitted
    CHECK(repo.log().size() == 2);                // commit #2 is still in the history
    CHECK(repo.nextUndo().empty());
    CHECK_THROWS(repo.undo(), NothingToUndoException);
}

TEST(undoTakesBackCheckoutSwitchAndNewBranches) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "v1\n");
    repo.track("notes.txt");
    repo.commit("first", "Swaraj");
    writeFile(dir, "notes.txt", "v2\n");
    repo.commit("second", "Swaraj");

    undoable(repo, "new branch feature", [&] { repo.createBranch("feature"); });
    undoable(repo, "switch to branch feature", [&] { repo.switchBranch("feature"); });
    undoable(repo, "checkout of commit #1", [&] { repo.checkout(1); });
    CHECK(readFile(dir, "notes.txt") == "v1\n");
    CHECK(repo.getCurrentBranch().empty());

    repo.undo();  // checkout
    CHECK(repo.getCurrentBranch() == "feature");
    CHECK(readFile(dir, "notes.txt") == "v2\n");
    repo.undo();  // switch
    CHECK(repo.getCurrentBranch() == "main");
    repo.undo();  // new branch
    CHECK(repo.getBranches().size() == 1);
}

TEST(undoTakesBackMerges) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "budget.txt", "Rent 1000\nFood 300\n");
    repo.track("budget.txt");
    repo.commit("first", "Swaraj");
    repo.createBranch("feature");
    writeFile(dir, "budget.txt", "Rent 1000\nFood 350\n");
    repo.commit("food 350", "Swaraj");
    repo.switchBranch("feature");
    writeFile(dir, "budget.txt", "Rent 1000\nFood 250\n");
    repo.commit("food 250", "Tanishka");
    repo.switchBranch("main");

    undoable(repo, "merge of branch feature", [&] { repo.merge("feature", "Swaraj"); });
    CHECK(repo.isMerging());
    repo.undo();  // works even though the files have conflict markers
    CHECK(!repo.isMerging());
    CHECK(readFile(dir, "budget.txt") == "Rent 1000\nFood 350\n");
    CHECK(!repo.hasUncommittedChanges());
}

TEST(undoRefusesToLoseUnsavedEdits) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    writeFile(dir, "notes.txt", "v1\n");
    repo.track("notes.txt");
    undoable(repo, "commit \"first\"", [&] { repo.commit("first", "Swaraj"); });
    writeFile(dir, "notes.txt", "typed but not saved\n");
    CHECK_THROWS(repo.undo(), UncommittedChangesException);
    CHECK(readFile(dir, "notes.txt") == "typed but not saved\n");
}

TEST(undoHistorySurvivesARestart) {
    TempDir dir;
    {
        Repository repo = Repository::init(dir.path());
        writeFile(dir, "notes.txt", "v1\n");
        repo.track("notes.txt");
        repo.commit("first", "Swaraj");
        undoable(repo, "new branch feature", [&] { repo.createBranch("feature"); });
    }
    Repository reopened = Repository::open(dir.path());
    CHECK(reopened.nextUndo() == "new branch feature");
    reopened.undo();
    CHECK(Repository::open(dir.path()).getBranches().size() == 1);
}
