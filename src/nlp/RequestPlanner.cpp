#include "nlp/RequestPlanner.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "nlp/IntentMatcher.h"

namespace {

// How many readings to offer at once. More than three stops being a question
// and starts being a menu.
constexpr std::size_t kMostOptions = 3;

std::string quoted(const std::string& text) {
    return "\"" + text + "\"";
}

std::string formattedTime(std::time_t when) {
    char buffer[32];
    const std::tm* local = std::localtime(&when);
    if (!local || std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", local) == 0) {
        return "an unknown time";
    }
    return buffer;
}

const CommitFact* findCommit(int id, const RequestContext& context) {
    for (const CommitFact& commit : context.commits) {
        if (commit.id == id) {
            return &commit;
        }
    }
    return nullptr;
}

// How many single-letter edits turn one word into the other. Used only to
// offer a near miss back, never to act on one.
std::size_t editDistance(const std::string& from, const std::string& to) {
    std::vector<std::size_t> row(to.size() + 1);
    for (std::size_t j = 0; j <= to.size(); ++j) {
        row[j] = j;
    }
    for (std::size_t i = 1; i <= from.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= to.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (from[i - 1] != to[j - 1] ? 1 : 0)});
            diagonal = above;
        }
    }
    return row[to.size()];
}

// The name in choices that typed is most likely a misspelling of, or empty.
// The allowance grows with the length of the name, so "man" can mean "main"
// while "dev" is not taken for "demo".
std::string closestName(const std::string& typed, const std::vector<std::string>& choices) {
    std::string best;
    std::size_t bestDistance = 0;
    for (const std::string& choice : choices) {
        const std::size_t distance = editDistance(typed, choice);
        const std::size_t allowed = std::min<std::size_t>(3, 1 + std::max(typed.size(), choice.size()) / 5);
        if (distance <= allowed && (best.empty() || distance < bestDistance)) {
            best = choice;
            bestDistance = distance;
        }
    }
    return best;
}

bool mentions(const std::vector<std::string>& words, const std::string& word) {
    return std::find(words.begin(), words.end(), word) != words.end();
}

}  // namespace

bool RequestPlanner::canLoseWork(IntentKind kind) {
    switch (kind) {
        // These four put old files back over what is in the folder, or throw
        // away a merge that was being fixed by hand.
        case IntentKind::Checkout:
        case IntentKind::Revert:
        case IntentKind::Undo:
        case IntentKind::AbortMerge:
            return true;
        // Switching and merging write files too, but only uncommitted work is
        // ever at risk, so they are asked about only when there is some.
        default:
            return false;
    }
}

std::string RequestPlanner::plainDescription(IntentKind kind) {
    switch (kind) {
        case IntentKind::Add: return "start tracking a file";
        case IntentKind::Commit: return "save a new version";
        case IntentKind::Log: return "list the commits";
        case IntentKind::Status: return "say what has changed since the last commit";
        case IntentKind::Diff: return "show which lines changed";
        case IntentKind::Show: return "show one commit's details";
        case IntentKind::Checkout: return "put a commit's files back without moving the branch";
        case IntentKind::Revert: return "put a commit's files back and save that as a new commit";
        case IntentKind::CreateBranch: return "start a new branch";
        case IntentKind::Switch: return "work on another branch";
        case IntentKind::ListBranches: return "list the branches";
        case IntentKind::Merge: return "bring another branch's changes into this one";
        case IntentKind::AbortMerge: return "cancel the merge that is waiting";
        case IntentKind::Undo: return "take back the last change";
        case IntentKind::Verify: return "check that no saved history was damaged";
        case IntentKind::Unknown: break;
    }
    return "";
}

RequestPlan RequestPlanner::plan(const std::string& request, const RequestContext& context) const {
    RequestPlan plan;
    const Intent intent = parser_.parse(request);

    // Unknown means either nothing matched at all, or two readings tied. A tie
    // is worth offering back; nothing at all is not.
    if (intent.kind == IntentKind::Unknown) {
        const std::vector<Intent> ranked = parser_.rank(request);
        if (ranked.empty()) {
            plan.outcome = RequestPlan::Outcome::Unclear;
            plan.problems.push_back("I didn't understand " + quoted(request) + ".");
            plan.problems.push_back(
                "Try something like \"save my work\", \"what changed today\" or \"go back to yesterday evening\".");
            return plan;
        }
        plan.outcome = RequestPlan::Outcome::DidYouMean;
        for (const Intent& option : ranked) {
            if (plan.options.size() >= kMostOptions || option.score < ranked.front().score) {
                break;
            }
            plan.options.push_back(plainDescription(option.kind) + " (chrono " + commandName(option.kind) + ")");
        }
        return plan;
    }

    const Details details = DetailExtractor::extract(intent, context);

    // A name that is nearly a real one is a question, not a refusal.
    if (!details.unknownName.empty()) {
        const std::string closest = closestName(details.unknownName, context.branches);
        if (!closest.empty()) {
            plan.outcome = RequestPlan::Outcome::DidYouMean;
            plan.options.push_back(plainDescription(intent.kind) + ", using " + closest + ", the branch closest to " +
                                   quoted(details.unknownName) + " (chrono " + commandName(intent.kind) + " " +
                                   closest + ")");
            return plan;
        }
    }

    if (!details.resolved()) {
        plan.outcome = RequestPlan::Outcome::Unclear;
        plan.problems = details.problems;
        return plan;
    }

    const std::vector<std::string> words = IntentMatcher::keyWords(intent.tokens);
    const std::string id = details.namesCommit() ? std::to_string(details.commitId()) : "";

    switch (intent.kind) {
        case IntentKind::Add:
            if (details.file.empty()) {
                plan.problems.push_back("I don't know which file to track. Try \"start tracking budget.csv\".");
                break;
            }
            plan.args = {"add", details.file};
            plan.action = "start tracking " + details.file;
            break;
        case IntentKind::Commit:
            if (details.message.empty()) {
                plan.problems.push_back(
                    "I don't know what to call this version. Put the message in quotes, as in "
                    "\"save my work as \\\"fixed totals\\\"\".");
                break;
            }
            plan.args = {"commit", "-m", details.message};
            plan.action = "save a new version called " + quoted(details.message);
            break;
        case IntentKind::Log:
            // "all" or "every" asks for the other branches' commits too.
            if (mentions(words, "all") || mentions(words, "every") || mentions(words, "everything")) {
                plan.args = {"log", "--all"};
                plan.action = "list every commit";
            } else {
                plan.args = {"log"};
                plan.action = "list this branch's commits";
            }
            break;
        case IntentKind::Status:
            plan.args = {"status"};
            plan.action = "show what has changed since the last commit";
            break;
        case IntentKind::Diff:
            if (details.commitIds.size() >= 2) {
                plan.args = {"diff", std::to_string(details.commitIds[0]), std::to_string(details.commitIds[1])};
                plan.action = "compare commit " + std::to_string(details.commitIds[0]) + " with commit " +
                              std::to_string(details.commitIds[1]);
            } else if (details.namesCommit() && !context.commits.empty()) {
                // One commit named means "what has changed since then".
                const int newest = context.commits.back().id;
                plan.args = {"diff", id, std::to_string(newest)};
                plan.action = "compare commit " + id + " with commit " + std::to_string(newest);
            } else {
                plan.args = {"diff"};
                plan.action = "show what has changed since the last commit";
            }
            break;
        case IntentKind::Show:
            if (!details.namesCommit()) {
                plan.problems.push_back("I don't know which commit to show. Try \"show commit 3\".");
                break;
            }
            plan.args = {"show", id};
            plan.action = "show commit " + id;
            break;
        case IntentKind::Checkout:
            if (!details.namesCommit()) {
                plan.problems.push_back(
                    "I don't know which version you mean. Try a number, as in \"check out commit 3\", or a time, "
                    "as in \"how it looked yesterday evening\".");
                break;
            }
            plan.args = {"checkout", id};
            plan.action = "put the files from commit " + id + " back, leaving the branch where it is";
            break;
        case IntentKind::Revert:
            if (!details.namesCommit()) {
                plan.problems.push_back(
                    "I don't know which version you mean. Try a number, as in \"revert to commit 3\", or a time, "
                    "as in \"go back to yesterday evening\".");
                break;
            }
            plan.args = {"revert", id};
            plan.action = "put the files from commit " + id + " back and save that as a new commit";
            break;
        case IntentKind::CreateBranch:
            plan.args = {"branch", details.branch};
            plan.action = "start a new branch called " + details.branch;
            break;
        case IntentKind::ListBranches:
            plan.args = {"branch"};
            plan.action = "list the branches";
            break;
        case IntentKind::Switch:
            plan.args = {"switch", details.branch};
            plan.action = "switch to the branch " + details.branch;
            break;
        case IntentKind::Merge:
            plan.args = {"merge", details.branch};
            plan.action = "bring " + details.branch + "'s changes into " +
                          (context.currentBranch.empty() ? "this commit" : context.currentBranch);
            break;
        case IntentKind::AbortMerge:
            if (!context.isMerging) {
                plan.problems.push_back("There is no merge waiting, so there is nothing to cancel.");
                break;
            }
            plan.args = {"merge", "--abort"};
            plan.action = "cancel the merge that is waiting";
            break;
        case IntentKind::Undo:
            if (context.undoable.empty()) {
                plan.problems.push_back("There is nothing to undo.");
                break;
            }
            plan.args = {"undo"};
            plan.action = "take back the " + context.undoable;
            break;
        case IntentKind::Verify:
            plan.args = {"verify"};
            plan.action = "check that no saved history was damaged";
            break;
        case IntentKind::Unknown:
            break;
    }

    if (!plan.problems.empty()) {
        plan.outcome = RequestPlan::Outcome::Unclear;
        plan.args.clear();
        plan.action.clear();
        return plan;
    }

    // Say how a time phrase was read, so a wrong reading is caught before the
    // files change rather than after.
    if (details.commitFromTime && details.namesCommit()) {
        const CommitFact* commit = findCommit(details.commitId(), context);
        const std::string& phrase = details.when.phrase;
        if (commit && details.usedCommitBefore) {
            plan.notes.push_back("Nothing was saved during " + phrase + ", so this uses commit " + id +
                                 ", the version in place at the time: " + quoted(commit->message) + ", saved " +
                                 formattedTime(commit->timestamp) + ".");
        } else if (commit) {
            plan.notes.push_back("By " + phrase + " you mean commit " + id + ": " + quoted(commit->message) +
                                 ", saved " + formattedTime(commit->timestamp) + ".");
        }
    }

    // Destructive commands always ask. Switching and merging ask only when
    // there is uncommitted work that they could write over.
    const bool risksUncommitted =
        context.hasUncommittedChanges && (intent.kind == IntentKind::Switch || intent.kind == IntentKind::Merge);
    if (canLoseWork(intent.kind) || risksUncommitted) {
        plan.outcome = RequestPlan::Outcome::Confirm;
        if (intent.kind == IntentKind::AbortMerge) {
            plan.question = "This throws away whatever you have fixed in the conflicted files. Go ahead?";
        } else if (risksUncommitted) {
            plan.question = "You have changes that no commit holds yet, and this may write over them. Go ahead?";
        } else if (context.hasUncommittedChanges) {
            plan.question = "You have changes that no commit holds yet, and this will replace them. Go ahead?";
        } else {
            plan.question = "Go ahead?";
        }
    } else {
        plan.outcome = RequestPlan::Outcome::Run;
    }
    return plan;
}
