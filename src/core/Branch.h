#pragma once

#include <string>

// A named line of work, such as "main" or "try-new-totals", pointing at its
// newest commit. A branch with no commits yet points at 0.
class Branch {
public:
    // Throws InvalidBranchNameException unless isValidName(name).
    Branch(std::string name, int commitId);

    const std::string& getName() const { return name_; }
    int getCommitId() const { return commitId_; }
    void moveTo(int commitId) { commitId_ = commitId; }

    // 1 to 50 letters, digits, '.', '_' or '-', not starting with '.' or '-'.
    // Branch names become file names, so this keeps them safe on every system.
    static bool isValidName(const std::string& name);

    // True when the names differ only in case. Mac and Windows treat such file
    // names as the same file, so they can't be two different branches.
    static bool sameNameIgnoringCase(const std::string& a, const std::string& b);

private:
    std::string name_;
    int commitId_;
};
