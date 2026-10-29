#pragma once

#include <string>
#include <vector>

#include "common/MergeResult.h"
#include "core/Repository.h"

// One conflict written out for the user: where it is, what happened, what each
// branch has, and what to do about it.
struct ConflictMessage {
    // The file and the place in it, e.g. "budget.csv, cell C2".
    std::string headline;
    // The strategy's own sentence, then one line per side and one for the
    // original, each either "label: value" or a label followed by a block.
    std::vector<std::string> details;
    // How to settle this kind of conflict by hand.
    std::string advice;
};

// Turns the conflicts a merge produced into plain English.
//
// The merge strategies can't do this themselves. A MergeStrategy is handed
// three snapshots and never learns which branches they came from, so its
// explanations can only say "this branch" and "the other branch"; the names
// only exist here, where the command knows what it was merging. The strategies
// stay the authority on *what happened*, and their sentence is used as it
// stands; this adds the branch names, the exact content each side has, and the
// advice, which a one-line explanation has no room for.
class ConflictMessages {
public:
    // ours and theirs are branch names. Either may be empty, when HEAD isn't
    // on a branch, and is then called "this commit".
    static ConflictMessage explain(const std::string& path, const Conflict& conflict, const std::string& ours,
                                   const std::string& theirs);

    // Every line to print when a merge stops at conflicts, in order and
    // without trailing newlines: the count, who "this branch" is, each
    // conflict, and how to finish or cancel.
    static std::vector<std::string> report(const MergeOutcome& outcome, const std::string& ours,
                                           const std::string& theirs);
};
