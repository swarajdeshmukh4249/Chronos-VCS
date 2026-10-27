#pragma once

#include <filesystem>
#include <set>
#include <string>

#include "core/Commit.h"
#include "core/History.h"

// Everything under .chrono/ is read and written here, so Repository never
// deals with file formats. Files are plain text so they can be opened and read:
//   .chrono/tracked          one tracked path per line
//   .chrono/commits/<id>.txt one commit (see StorageManager.cpp for the format)
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

    void saveCommit(const Commit& commit) const;
    // Commits 1..n in order. Throws CorruptObjectException if one is missing or malformed.
    History<Commit> loadCommits() const;

    void saveTracked(const std::set<std::string>& paths) const;
    std::set<std::string> loadTracked() const;

private:
    std::filesystem::path folder_;
};
