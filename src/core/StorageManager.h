#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "core/Commit.h"
#include "core/History.h"

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
//   .chrono/HEAD             id of the commit the working files came from (0 = none yet)
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

    void saveHead(int id) const;
    // Throws CorruptObjectException if HEAD is missing or not a number.
    int loadHead() const;

private:
    int highestCommitId() const;
    std::filesystem::path commitPath(int id) const;
    // e.g. "Commit 2 (.chrono/commits/2.txt)", for error messages.
    static std::string commitName(int id);

    std::filesystem::path folder_;
};
