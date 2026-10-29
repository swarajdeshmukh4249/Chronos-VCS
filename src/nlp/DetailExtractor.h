#pragma once

#include <string>
#include <vector>

#include "nlp/Intent.h"
#include "nlp/RequestContext.h"
#include "nlp/TimeWords.h"

// Everything a request names besides the command itself. An Intent says *what*
// to do; Details says *to what*.
struct Details {
    // An existing branch, matched whatever case it was typed in, or the new
    // name when the request is asking for a branch to be created.
    std::string branch;
    // A tracked file, or a path that isn't tracked yet when the request is "add".
    std::string file;
    // The quoted part of a commit request: commit this as "fixed totals".
    std::string message;
    // The commits the request names, in the order they were named. A diff can
    // use two ("compare 2 and 5"); every other command uses the first.
    std::vector<int> commitIds;

    // The time phrase the request used, when it had one.
    TimeWindow when;
    // commitIds was worked out from when rather than from a number the user gave.
    bool commitFromTime = false;
    // Nothing was saved inside when, so the newest commit from *before* it was
    // used instead: that is the version that was in place at the time.
    bool usedCommitBefore = false;

    // Anything that didn't add up, already written as a sentence for the user:
    // a branch that doesn't exist, a commit number past the end of the history,
    // a time from before the project started. Empty means everything resolved.
    std::vector<std::string> problems;

    bool namesCommit() const { return !commitIds.empty(); }
    // 0 when no commit was named.
    int commitId() const { return commitIds.empty() ? 0 : commitIds.front(); }
    bool resolved() const { return problems.empty(); }
};

// Picks the details out of a parsed request, checking them against what the
// repository actually holds, so "switch to experiment" fails here rather than
// deeper in the core, and "yesterday evening" comes back as a commit number.
class DetailExtractor {
public:
    static Details extract(const Intent& intent, const RequestContext& context);

    // The commit showing how things stood during window: the newest one saved
    // inside it, or else the newest one saved before it, since that is the
    // version that was in place at the time. 0 when the history starts later.
    // insideWindow says which of the two it was.
    static int commitAt(const TimeWindow& window, const RequestContext& context, bool& insideWindow);
};
