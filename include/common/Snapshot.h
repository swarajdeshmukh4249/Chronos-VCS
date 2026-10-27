#pragma once

#include <string>
#include <utility>

// The state of one tracked file at a point in time.
// Kept minimal so both layers can depend on it; src/core builds on this type.
class Snapshot {
public:
    Snapshot(std::string path, std::string content)
        : path_(std::move(path)), content_(std::move(content)) {}

    const std::string& getPath() const { return path_; }
    const std::string& getContent() const { return content_; }

private:
    std::string path_;
    std::string content_;
};
