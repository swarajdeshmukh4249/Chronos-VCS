#include "TestHarness.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "TempDir.h"
#include "common/Exceptions.h"
#include "core/Command.h"

namespace {

std::unique_ptr<Command> make(const std::vector<std::string>& args) {
    return CommandFactory::create(args, "Swaraj");
}

// Runs a command the way the command line does and returns what it printed.
std::string run(Repository& repo, const std::vector<std::string>& args, int expectedCode = 0) {
    std::ostringstream out;
    const int code = make(args)->run(repo, out);
    CHECK(code == expectedCode);
    return out.str();
}

}  // namespace

// Issue #14
TEST(factoryBuildsTheMatchingCommand) {
    CHECK(make({"add", "a.txt", "b.txt"})->describe() == "add a.txt, b.txt");
    CHECK(make({"commit", "-m", "fix totals"})->describe() == "commit \"fix totals\"");
    CHECK(make({"checkout", "3"})->describe() == "checkout of commit #3");
    CHECK(make({"revert", "1"})->describe() == "revert to commit #1");
    CHECK(make({"branch", "feature"})->describe() == "new branch feature");
    CHECK(make({"switch", "feature"})->describe() == "switch to branch feature");
    CHECK(make({"merge", "feature"})->describe() == "merge of branch feature");
    CHECK(make({"merge", "--abort"})->describe() == "cancelled merge");
    CHECK(make({"undo"})->describe() == "undo");
}

TEST(factoryLeavesOtherWordsAlone) {
    for (const std::vector<std::string>& args : std::vector<std::vector<std::string>>{
             {}, {"log"}, {"status"}, {"branch"}, {"diff", "1", "2"}, {"commit"}, {"commit", "fix"}, {"add"}}) {
        CHECK(make(args) == nullptr);
    }
    CHECK_THROWS(make({"checkout", "abc"}), CommitNotFoundException);
    CHECK_THROWS(make({"revert", "2x"}), CommitNotFoundException);
}

TEST(commandsRunAndCanBeUndone) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    WorkingTree(dir.path()).write("notes.txt", "v1\n");
    // AddCommand resolves paths against the current folder, so pass an absolute one.
    CHECK(run(repo, {"add", (dir.path() / "notes.txt").string()}) == "Tracking notes.txt\n");
    CHECK(run(repo, {"commit", "-m", "first"}) == "Saved commit #1 (1 file)\n");
    run(repo, {"branch", "feature"});
    CHECK(repo.nextUndo() == "new branch feature");

    CHECK(run(repo, {"undo"}).rfind("Undid the new branch feature.", 0) == 0);
    CHECK(repo.getBranches().size() == 1);
    // undo isn't itself undoable, so the next undo takes back the commit.
    CHECK(repo.nextUndo() == "commit \"first\"");
}

TEST(failedCommandsAreNotRemembered) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    CHECK_THROWS(make({"commit", "-m", "nothing tracked"})->run(repo, std::cout), NothingToCommitException);
    CHECK_THROWS(make({"switch", "nope"})->run(repo, std::cout), BranchNotFoundException);
    CHECK(repo.nextUndo().empty());
}

TEST(mergeCommandReportsConflictsWithExitCodeOne) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    WorkingTree tree(dir.path());
    tree.write("budget.txt", "Food 300\n");
    repo.track("budget.txt");
    repo.commit("first", "Swaraj");
    repo.createBranch("feature");
    tree.write("budget.txt", "Food 350\n");
    repo.commit("food 350", "Swaraj");
    repo.switchBranch("feature");
    tree.write("budget.txt", "Food 250\n");
    repo.commit("food 250", "Tanishka");
    repo.switchBranch("main");

    const std::string output = run(repo, {"merge", "feature"}, 1);
    CHECK(output.find("Merge stopped: 1 conflict to fix.") != std::string::npos);
    CHECK(output.find(".csv") == std::string::npos);
    CHECK(repo.nextUndo() == "merge of branch feature");
}

// Issue #15
TEST(mergeCommandExplainsConflictsInsideSheetCells) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    WorkingTree tree(dir.path());
    tree.write("budget.csv", "Item,Cost\nFood,300\n");
    repo.track("budget.csv");
    repo.commit("first", "Swaraj");
    repo.createBranch("feature");
    tree.write("budget.csv", "Item,Cost\nFood,350\n");
    repo.commit("food 350", "Swaraj");
    repo.switchBranch("feature");
    tree.write("budget.csv", "Item,Cost\nFood,250\n");
    repo.commit("food 250", "Tanishka");
    repo.switchBranch("main");

    const std::string output = run(repo, {"merge", "feature"}, 1);
    CHECK(output.find("budget.csv, B2: ") != std::string::npos);
    CHECK(output.find("In a .csv file both values sit inside the cell") != std::string::npos);
}
