#include "core/Branch.h"

#include <cctype>
#include <utility>

#include "common/Exceptions.h"

Branch::Branch(std::string name, int commitId) : name_(std::move(name)), commitId_(commitId) {
    if (!isValidName(name_)) {
        throw InvalidBranchNameException("\"" + name_ +
                                         "\" can't be a branch name. Use letters, digits, '.', '_' or '-'.");
    }
}

bool Branch::isValidName(const std::string& name) {
    if (name.empty() || name.size() > 50 || name[0] == '.' || name[0] == '-') {
        return false;
    }
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

bool Branch::sameNameIgnoringCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}
