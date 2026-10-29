#pragma once

#include <ctime>
#include <string>
#include <vector>

// One commit, as much of it as the plain-English layer needs.
struct CommitFact {
    int id = 0;
    std::time_t timestamp = 0;
    std::string message;
};

// What the repository holds that a request can point at: "switch to experiment"
// only means something if a branch of that name exists, and "yesterday evening"
// only resolves if some commit was saved around then.
//
// The plain-English layer reads this instead of Repository, so src/nlp never
// includes src/core (Dependency Inversion), and tests can hand it a made-up
// history with fixed timestamps instead of building a real repository.
struct RequestContext {
    std::vector<std::string> branches;
    std::string currentBranch;
    // Paths as the repository stores them, relative to the root.
    std::vector<std::string> trackedFiles;
    // Oldest first, so ids run 1, 2, 3... in step with the vector.
    std::vector<CommitFact> commits;
    // What "yesterday" and "2 hours ago" are counted back from.
    std::time_t now = 0;
};
