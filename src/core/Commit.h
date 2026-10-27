#pragma once

#include <ctime>
#include <map>
#include <string>

#include "common/Snapshot.h"

// One saved version of the project: every tracked file's content, plus who
// saved it, when, and why.
class Commit {
public:
    // Keyed by path, so files are always listed in the same order.
    using Files = std::map<std::string, Snapshot>;

    // parentId is 0 for the first commit. A merge commit also records the commit
    // it merged in as mergedParentId; other commits leave it 0.
    Commit(int id, int parentId, std::string author, std::string message, std::time_t timestamp,
           Files files, int mergedParentId = 0);

    int getId() const { return id_; }
    int getParentId() const { return parentId_; }
    bool hasParent() const { return parentId_ != 0; }
    int getMergedParentId() const { return mergedParentId_; }
    bool isMerge() const { return mergedParentId_ != 0; }
    const std::string& getAuthor() const { return author_; }
    const std::string& getMessage() const { return message_; }
    std::time_t getTimestamp() const { return timestamp_; }
    const Files& getFiles() const { return files_; }

    // nullptr when the file is not part of this commit.
    const Snapshot* findFile(const std::string& path) const;

    // True when files has exactly the same paths and contents as this commit.
    bool hasSameFiles(const Files& files) const;

    // Local time, e.g. "2026-10-27 14:05".
    std::string formattedTime() const;

    // One line for the log, e.g. "#3  2026-10-27 14:05  Swaraj  fixed totals".
    std::string summary() const;

private:
    int id_;
    int parentId_;
    std::string author_;
    std::string message_;
    std::time_t timestamp_;
    Files files_;
    int mergedParentId_;
};
