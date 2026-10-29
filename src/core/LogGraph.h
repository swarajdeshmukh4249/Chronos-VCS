#pragma once

#include <string>
#include <vector>

class Repository;

// Every commit, newest first, with its branches and merges drawn in ASCII beside it:
//
//   *   #4  checked the budget  (HEAD, main)
//   | \ .
//   | * #3  warn when over 20  (experiment)
//   * | #2  rent is 50
//   | /
//   * #1  base
//
// Each column is a line of history waiting for its next commit. A merge opens a
// second column for the branch it brought in, and columns that reach the same
// commit join again.
class LogGraph {
public:
    static std::vector<std::string> draw(const Repository& repo);
};
