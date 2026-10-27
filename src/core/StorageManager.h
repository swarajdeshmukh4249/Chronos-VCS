#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/Branch.h"
#include "core/Commit.h"
#include "core/History.h"

// What HEAD points at: normally a branch, or a commit directly after checking
// out an old commit ("not on a branch").
struct HeadRef {
    std::string branch;  // empty when HEAD points straight at a commit
    int commitId = 0;    // used only when branch is empty
};

// A merge that stopped at conflicts and waits for the user to fix the files and commit.
struct PendingMerge {
    int commitId = 0;  // the commit being merged in; 0 when no merge is waiting
    std::string branch;
};

// Memento pattern: everything `undo` needs to put the repository back as it was
// before a command. Working files are stored as content hashes in objects/.
struct RepositoryState {
    std::string description;  // the command this state came before, e.g. "commit \"fix totals\""
    HeadRef head;
    std::vector<Branch> branches;
    std::set<std::string> tracked;
    PendingMerge merge;
    std::map<std::string, std::string> files;  // path -> content hash
};

// One thing `verify` found wrong, e.g. {".chrono/objects/af63...", "was changed (notes.txt in commit #2)"}.
struct IntegrityProblem {
    std::string file;
    std::string problem;
};

struct IntegrityReport {
    int commits = 0;  // commit files that could be read
    int objects = 0;  // distinct file contents checked
    std::vector<IntegrityProblem> problems;
};

// Everything under .chrono/ is read and written here, so Repository never
// deals with file formats. Files are plain text so they can be opened and read:
//   .chrono/HEAD             "ref: main" on a branch, or a commit id when not on one
//   .chrono/branches/<name>  id of the branch's newest commit (0 = none yet)
//   .chrono/MERGE            "<commit id> <branch>" while a merge waits on conflicts
//   .chrono/undo/<n>.txt     what to restore for each undoable command, newest highest
//   .chrono/tracked          one tracked path per line
//   .chrono/commits/<id>.txt one commit (see StorageManager.cpp for the format)
//   .chrono/objects/<hash>   one version of a file, named by its content hash
class StorageManager {
public:
    static constexpr const char* kFolderName = ".chrono";

    // root is the project folder that contains (or will contain) .chrono/.
    explicit StorageManager(const std::filesystem::path& root);

    // Searches start and its parent folders for .chrono/.
    // Throws RepositoryNotFoundException.
    static std::filesystem::path findRoot(const std::filesystem::path& start);

    bool isInitialized() const;
    // Throws RepositoryExistsException if .chrono/ already exists.
    void initialize() const;

    // Writes each file's content to objects/ unless that content is already stored.
    void saveCommit(const Commit& commit) const;
    // Commits 1..n in order. Throws CorruptObjectException if one is missing or malformed.
    History<Commit> loadCommits() const;

    // Re-hashes every commit file and object and reports anything that doesn't match.
    IntegrityReport verify() const;

    void saveTracked(const std::set<std::string>& paths) const;
    std::set<std::string> loadTracked() const;

    void saveHead(const HeadRef& head) const;
    // Throws CorruptObjectException if HEAD is missing or malformed.
    HeadRef loadHead() const;

    // Saving a merge with commitId 0 clears it.
    void savePendingMerge(const PendingMerge& merge) const;
    // commitId is 0 when no merge is waiting.
    PendingMerge loadPendingMerge() const;

    void saveBranch(const Branch& branch) const;
    // Sorted by name. Throws CorruptObjectException if a branch file is malformed.
    std::vector<Branch> loadBranches() const;
    // Does nothing if the branch has no file.
    void deleteBranch(const std::string& name) const;

    // Stores content in objects/ (once) and returns its hash.
    std::string saveObject(const std::string& content) const;
    // Throws CorruptObjectException if the object is missing.
    std::string loadObject(const std::string& hash) const;

    // The undo history is a stack of states in .chrono/undo/<n>.txt.
    void pushUndo(const RepositoryState& state) const;
    bool hasUndo() const;
    // The most recent state. Throws NothingToUndoException when the stack is empty.
    RepositoryState peekUndo() const;
    void popUndo() const;

private:
    int highestUndoNumber() const;

    int highestCommitId() const;
    std::filesystem::path commitPath(int id) const;
    // e.g. "Commit 2 (.chrono/commits/2.txt)", for error messages.
    static std::string commitName(int id);

    std::filesystem::path folder_;
};
