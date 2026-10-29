#pragma once

#include <string>
#include <vector>

#include "nlp/Tokenizer.h"

// What a plain-English request asks Chrono to do, one kind per command.
enum class IntentKind {
    Unknown,
    Add,
    Commit,
    Log,
    Status,
    Diff,
    Show,
    Checkout,
    Revert,
    CreateBranch,
    Switch,
    ListBranches,
    Merge,
    AbortMerge,
    Undo,
    Verify,
};

// The command a kind stands for, as typed on the command line: Revert -> "revert",
// AbortMerge -> "merge --abort", Unknown -> "".
std::string commandName(IntentKind kind);

// A guess at what the user meant. score says how strongly the words point at
// kind (0 for none), and tokens keeps the whole request so the details, such as
// which commit or branch, can be picked out afterwards.
struct Intent {
    IntentKind kind = IntentKind::Unknown;
    int score = 0;
    std::vector<Token> tokens;
};
