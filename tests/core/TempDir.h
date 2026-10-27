#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

// A fresh, empty folder under the system temp directory, deleted when it goes out of scope.
class TempDir {
public:
    TempDir() {
        std::random_device random;
        path_ = std::filesystem::temp_directory_path() /
                ("chrono-test-" + std::to_string(random()) + "-" + std::to_string(++counter()));
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    static int& counter() {
        static int count = 0;
        return count;
    }

    std::filesystem::path path_;
};
