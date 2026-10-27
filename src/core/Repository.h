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

    // Saves every tracked file as a new commit. Throws NothingToCommitException
    // when nothing is tracked or nothing changed since the last commit.
    const Commit& commit(const std::string& message, const std::string& author);

    const History<Commit>& log() const { return commits_; }
    // Throws CommitNotFoundException.
    const Commit& getCommit(int id) const;

private:
    explicit Repository(const std::filesystem::path& root);

    // The tracked files as they are on disk right now. Deleted files are left out.
    Commit::Files currentFiles() const;

    WorkingTree tree_;
    StorageManager storage_;
    History<Commit> commits_;
    std::set<std::string> tracked_;
};
