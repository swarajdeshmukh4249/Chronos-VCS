#include "TestHarness.h"

#include <string>
#include <vector>

#include "TempDir.h"
#include "core/LogGraph.h"
#include "core/Repository.h"
#include "core/WorkingTree.h"

// Issue #20
TEST(logGraphDrawsABranchAndItsMerge) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    WorkingTree tree(dir.path());
    tree.write("a.txt", "1\n");
    repo.track("a.txt");
    repo.commit("base", "T");
    repo.createBranch("experiment");
    tree.write("a.txt", "1\n2\n");
    repo.commit("main work", "T");
    repo.switchBranch("experiment");
    tree.write("b.txt", "x\n");
    repo.track("b.txt");
    repo.commit("experiment work", "T");
    repo.switchBranch("main");
    repo.merge("experiment", "T");

    const std::vector<std::string> expected = {
        "*   #4  Merge branch \"experiment\" into \"main\"  (HEAD, main)",
        "| \\",
        "| * #3  experiment work  (experiment)",
        "* | #2  main work",
        "| /",
        "* #1  base",
    };
    const std::vector<std::string> lines = LogGraph::draw(repo);
    CHECK(lines.size() == expected.size());
    for (std::size_t i = 0; i < expected.size() && i < lines.size(); ++i) {
        CHECK(lines[i] == expected[i]);
    }
}

TEST(logGraphOfAnEmptyRepositoryIsEmpty) {
    TempDir dir;
    CHECK(LogGraph::draw(Repository::init(dir.path())).empty());
}

// Issue #21
TEST(latestCommitBeforeFindsTheVersionInPlaceAtThatTime) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    WorkingTree tree(dir.path());
    tree.write("a.txt", "1\n");
    repo.track("a.txt");
    const std::time_t first = repo.commit("one", "T").getTimestamp();
    tree.write("a.txt", "2\n");
    repo.commit("two", "T");

    CHECK(repo.latestCommitBefore(first) == 0);
    CHECK(repo.latestCommitBefore(first + 100000) == 2);
}
