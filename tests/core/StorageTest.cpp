#include "TestHarness.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "TempDir.h"
#include "common/Exceptions.h"
#include "core/StorageManager.h"
#include "core/WorkingTree.h"

namespace fs = std::filesystem;

namespace {

Commit sampleCommit(int id, const std::string& content) {
    Commit::Files files;
    files.emplace("notes.txt", Snapshot("notes.txt", content));
    files.emplace("data/budget.csv", Snapshot("data/budget.csv", "A1,B1\n10,=A1*2\n"));
    return Commit(id, id - 1, "Swaraj", "message with\nnewline", 1793015606, files);
}

void overwrite(const fs::path& path, const std::string& content) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
}

}  // namespace

// Issue #5
TEST(workingTreeReadsAndWritesFiles) {
    TempDir dir;
    WorkingTree tree(dir.path());
    tree.write("data/budget.csv", "A1,10\n");
    CHECK(tree.exists("data/budget.csv"));
    CHECK(tree.read("data/budget.csv") == "A1,10\n");
    tree.remove("data/budget.csv");
    CHECK(!tree.exists("data/budget.csv"));
    CHECK_THROWS(tree.read("data/budget.csv"), FileNotFoundException);
}

TEST(workingTreeNormalisesPaths) {
    TempDir dir;
    WorkingTree tree(dir.path());
    CHECK(tree.relativePath("notes.txt") == "notes.txt");
    CHECK(tree.relativePath("./data/../notes.txt") == "notes.txt");
    CHECK(tree.relativePath(dir.path() / "data" / "budget.csv") == "data/budget.csv");
    CHECK_THROWS(tree.relativePath("../outside.txt"), FileNotFoundException);
    CHECK_THROWS(tree.relativePath(".chrono/tracked"), FileNotFoundException);
    CHECK_THROWS(tree.relativePath("."), FileNotFoundException);
}

TEST(storageInitialisesOnce) {
    TempDir dir;
    StorageManager storage(dir.path());
    CHECK(!storage.isInitialized());
    storage.initialize();
    CHECK(storage.isInitialized());
    CHECK(storage.loadCommits().empty());
    CHECK(storage.loadTracked().empty());
    CHECK_THROWS(storage.initialize(), RepositoryExistsException);
}

TEST(storageRoundTripsCommits) {
    TempDir dir;
    StorageManager storage(dir.path());
    storage.initialize();
    storage.saveCommit(sampleCommit(1, "first"));
    storage.saveCommit(sampleCommit(2, "second\nwith two lines\n"));

    const History<Commit> history = StorageManager(dir.path()).loadCommits();
    CHECK(history.size() == 2);
    const Commit& second = history.at(2);
    CHECK(second.getId() == 2);
    CHECK(second.getParentId() == 1);
    CHECK(second.getAuthor() == "Swaraj");
    CHECK(second.getMessage() == "message with\nnewline");
    CHECK(second.getTimestamp() == 1793015606);
    CHECK(second.findFile("notes.txt")->getContent() == "second\nwith two lines\n");
    CHECK(second.findFile("data/budget.csv")->getContent() == "A1,B1\n10,=A1*2\n");
}

TEST(storageRoundTripsTrackedFiles) {
    TempDir dir;
    StorageManager storage(dir.path());
    storage.initialize();
    storage.saveTracked({"notes.txt", "data/budget.csv"});
    CHECK(storage.loadTracked() == (std::set<std::string>{"data/budget.csv", "notes.txt"}));
}

TEST(storageDetectsDamagedCommits) {
    TempDir dir;
    StorageManager storage(dir.path());
    storage.initialize();
    storage.saveCommit(sampleCommit(1, "first"));
    const fs::path file = dir.path() / ".chrono" / "commits" / "1.txt";

    overwrite(file, "id 1\nparent 0\nauthor Swaraj\n");
    CHECK_THROWS(storage.loadCommits(), CorruptObjectException);

    overwrite(file, "id 1\nparent 0\nauthor Swaraj\ntime 5\nmessage 50\nshort\n");
    CHECK_THROWS(storage.loadCommits(), CorruptObjectException);

    overwrite(file, "id 7\nparent 0\nauthor Swaraj\ntime 5\nmessage 2\nhi\n");
    CHECK_THROWS(storage.loadCommits(), CorruptObjectException);
}

TEST(storageDetectsMissingCommits) {
    TempDir dir;
    StorageManager storage(dir.path());
    storage.initialize();
    storage.saveCommit(sampleCommit(1, "first"));
    storage.saveCommit(sampleCommit(3, "third"));
    CHECK_THROWS(storage.loadCommits(), CorruptObjectException);
}

TEST(findRootSearchesParentFolders) {
    TempDir dir;
    StorageManager(dir.path()).initialize();
    fs::create_directories(dir.path() / "a" / "b");
    CHECK(StorageManager::findRoot(dir.path() / "a" / "b") == fs::weakly_canonical(dir.path()));

    TempDir empty;
    CHECK_THROWS(StorageManager::findRoot(empty.path()), RepositoryNotFoundException);
}

TEST(storageRoundTripsHead) {
    TempDir dir;
    StorageManager storage(dir.path());
    storage.initialize();
    CHECK(storage.loadHead() == 0);
    storage.saveHead(12);
    CHECK(storage.loadHead() == 12);
    overwrite(dir.path() / ".chrono" / "HEAD", "abc\n");
    CHECK_THROWS(storage.loadHead(), CorruptObjectException);
}
