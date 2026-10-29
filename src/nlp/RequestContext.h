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

    // Whether there is work in the folder that no commit holds yet, so a
    // request that overwrites files knows to warn before it does.
    bool hasUncommittedChanges = false;
    // A merge is waiting on conflicts, so cancelling it would throw away
    // whatever has been fixed by hand so far.
    bool isMerging = false;
    // What `undo` would take back, in the words Repository::nextUndo uses, or
    // empty when there is nothing to undo.
    std::string undoable;
};
