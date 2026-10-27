#pragma once

#include <filesystem>
#include <string>

// The user's own files in the project folder. Paths are relative to the root
// and use '/' on every platform, e.g. "data/budget.csv".
class WorkingTree {
public:
    explicit WorkingTree(const std::filesystem::path& root);

    const std::filesystem::path& getRoot() const { return root_; }

    // Turns an absolute path, or one relative to the root, into the form above.
    // Throws FileNotFoundException for paths outside the project or inside .chrono/.
    std::string relativePath(const std::filesystem::path& path) const;

    bool exists(const std::string& path) const;
    // Throws FileNotFoundException.
    std::string read(const std::string& path) const;
    // Creates missing parent folders. Throws ChronoException if the file can't be written.
    void write(const std::string& path, const std::string& content) const;
    // Does nothing if the file is already gone.
    void remove(const std::string& path) const;

private:
    std::filesystem::path root_;
};
