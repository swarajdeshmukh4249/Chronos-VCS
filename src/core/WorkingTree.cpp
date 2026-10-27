#include "core/WorkingTree.h"

#include <fstream>
#include <sstream>

#include "common/Exceptions.h"

namespace fs = std::filesystem;

WorkingTree::WorkingTree(const fs::path& root) : root_(fs::weakly_canonical(fs::absolute(root))) {}

std::string WorkingTree::relativePath(const fs::path& path) const {
    const fs::path full = fs::weakly_canonical(path.is_absolute() ? path : root_ / path);
    const fs::path relative = full.lexically_relative(root_);
    const std::string first = relative.empty() ? "" : relative.begin()->string();
    if (relative.empty() || first == ".." || first == "." || first == ".chrono") {
        throw FileNotFoundException("\"" + path.string() + "\" is not a file inside this project");
    }
    return relative.generic_string();
}

bool WorkingTree::exists(const std::string& path) const {
    return fs::is_regular_file(root_ / path);
}

std::string WorkingTree::read(const std::string& path) const {
    std::ifstream in(root_ / path, std::ios::binary);
    if (!exists(path) || !in) {
        throw FileNotFoundException("No file named \"" + path + "\"");
    }
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

void WorkingTree::write(const std::string& path, const std::string& content) const {
    const fs::path full = root_ / path;
    std::error_code ignored;
    fs::create_directories(full.parent_path(), ignored);
    std::ofstream out(full, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) {
        throw ChronoException("Could not write \"" + path + "\"");
    }
}

void WorkingTree::remove(const std::string& path) const {
    std::error_code ignored;
    fs::remove(root_ / path, ignored);
}
