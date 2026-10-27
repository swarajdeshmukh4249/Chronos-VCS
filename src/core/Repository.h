#pragma once

#include <filesystem>
#include <set>
#include <string>

#include "core/Commit.h"
#include "core/History.h"
#include "core/StorageManager.h"
#include "core/WorkingTree.h"

// A project's version history. StorageManager handles .chrono/ and WorkingTree
// handles the user's files, so this class only holds the version-control rules.
class Repository {
public:
    // Creates .chrono/ in root. Throws RepositoryExistsException.
    static Repository init(const std::filesystem::path& root);
    // Opens the repository that contains start. Throws RepositoryNotFoundException.
    static Repository open(const std::filesystem::path& start);

    const std::filesystem::path& getRoot() const { return tree_.getRoot(); }

    // Includes the file in every later commit and returns its path relative to
    // the root. Throws FileNotFoundException.
    std::string track(const std::filesystem::path& path);
    const std::set<std::string>& getTracked() const { return tracked_; }

    // Saves every tracked file as a new commit whose parent is the HEAD commit.
    // Throws NothingToCommitException when nothing is tracked or nothing changed.
    const Commit& commit(const std::string& message, const std::string& author);

    // The commit the working files came from, or 0 before the first commit.
    int getHead() const { return head_; }

    const History<Commit>& log() const { return commits_; }
    // Throws CommitNotFoundException.
    const Commit& getCommit(int id) const;

    // True when a tracked file differs from the HEAD commit.
    bool hasUncommittedChanges() const;

    // Puts the files from commit id back on disk and makes it HEAD, without
    // recording a commit. Throws CommitNotFoundException, or
    // UncommittedChangesException rather than overwrite unsaved work.
    void checkout(int id);

    // Puts the files from commit id back and records that as a new commit on top
    // of HEAD, so no history is lost. Throws like checkout, or
    // NothingToCommitException if the files already match.
    const Commit& revert(int id, const std::string& author);

private:
    explicit Repository(const std::filesystem::path& root);

    // The tracked files as they are on disk right now. Deleted files are left out.
    Commit::Files currentFiles() const;
    // Throws UncommittedChangesException.
    void requireNoUncommittedChanges() const;
    // Writes target's files and removes tracked files it doesn't have.
    void restoreFiles(const Commit& target);

    WorkingTree tree_;
    StorageManager storage_;
    History<Commit> commits_;
    std::set<std::string> tracked_;
    int head_;
};
