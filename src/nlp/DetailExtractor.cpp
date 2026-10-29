#include "nlp/DetailExtractor.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "nlp/IntentMatcher.h"

namespace {

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool sameName(const std::string& a, const std::string& b) {
    return lowered(a) == lowered(b);
}

// "main", "main and demo", "main, demo and experiment".
std::string listNames(const std::vector<std::string>& names) {
    std::string listed;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            listed += i + 1 == names.size() ? " and " : ", ";
        }
        listed += names[i];
    }
    return listed;
}

// A word that introduces a name instead of being one: "a branch called fix".
bool isNameLeadIn(const std::string& word) {
    static const std::set<std::string> leadIns = {"called", "named", "branch", "to", "into", "onto", "from"};
    return leadIns.count(word) > 0;
}

bool isCommitNoun(const std::string& word) {
    static const std::set<std::string> nouns = {"commit",   "commits",   "version",  "versions",
                                                "snapshot", "snapshots", "revision", "save"};
    return nouns.count(word) > 0;
}

// Looks like a path rather than a word: a dot with a short all-letter extension
// after it, so "budget.csv" counts while "3.5" and "e.g" do not. A slash is
// enough on its own, for "data/budget".
bool looksLikeFile(const std::string& text) {
    const std::size_t dot = text.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == text.size()) {
        return text.find('/') != std::string::npos;
    }
    const std::string extension = text.substr(dot + 1);
    return extension.size() <= 5 && std::all_of(extension.begin(), extension.end(), [](char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0;
    });
}

// The tracked path that text names, by whole path or by file name alone, or empty.
std::string matchTrackedFile(const std::string& text, const RequestContext& context) {
    for (const std::string& tracked : context.trackedFiles) {
        if (sameName(tracked, text)) {
            return tracked;
        }
    }
    for (const std::string& tracked : context.trackedFiles) {
        const std::size_t slash = tracked.find_last_of("/\\");
        const std::string name = slash == std::string::npos ? tracked : tracked.substr(slash + 1);
        if (sameName(name, text)) {
            return tracked;
        }
    }
    return {};
}

const CommitFact* newestCommit(const RequestContext& context) {
    const CommitFact* newest = nullptr;
    for (const CommitFact& commit : context.commits) {
        if (!newest || commit.id > newest->id) {
            newest = &commit;
        }
    }
    return newest;
}

bool knownCommitId(int id, const RequestContext& context) {
    return std::any_of(context.commits.begin(), context.commits.end(),
                       [id](const CommitFact& commit) { return commit.id == id; });
}

bool isNumber(const std::string& word) {
    return !word.empty() && std::all_of(word.begin(), word.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

}  // namespace

int DetailExtractor::commitAt(const TimeWindow& window, const RequestContext& context, bool& insideWindow) {
    insideWindow = false;
    const CommitFact* best = nullptr;
    for (const CommitFact& commit : context.commits) {
        if (window.contains(commit.timestamp) && (!best || commit.timestamp >= best->timestamp)) {
            best = &commit;
        }
    }
    if (best) {
        insideWindow = true;
        return best->id;
    }
    // Nothing was saved during the window, so the version in place at the time
    // is whatever was saved last before it.
    for (const CommitFact& commit : context.commits) {
        if (commit.timestamp < window.from && (!best || commit.timestamp >= best->timestamp)) {
            best = &commit;
        }
    }
    return best ? best->id : 0;
}

Details DetailExtractor::extract(const Intent& intent, const RequestContext& context) {
    Details details;
    const std::vector<Token>& tokens = intent.tokens;
    const std::vector<std::string> words = IntentMatcher::keyWords(tokens);

    // The commit message is whatever was quoted: commit this as "fixed totals".
    for (const Token& token : tokens) {
        if (token.quoted) {
            details.message = token.text;
            break;
        }
    }

    // Every existing branch the request mentions. The one meant is the first
    // that isn't the branch we are already on, which picks "experiment" out of
    // "merge experiment into main" without having to read the word order.
    std::vector<std::string> mentioned;
    for (const Token& token : tokens) {
        if (token.quoted) {
            continue;
        }
        for (const std::string& branch : context.branches) {
            if (sameName(branch, token.text) &&
                std::find(mentioned.begin(), mentioned.end(), branch) == mentioned.end()) {
                mentioned.push_back(branch);
            }
        }
    }
    for (const std::string& branch : mentioned) {
        if (branch != context.currentBranch) {
            details.branch = branch;
            break;
        }
    }
    if (details.branch.empty() && !mentioned.empty()) {
        details.branch = mentioned.front();
    }

    // A file, tracked or not: "add budget.csv" names one that isn't tracked yet.
    // A tracked file wins over a guess from the spelling.
    for (const Token& token : tokens) {
        if (token.quoted) {
            continue;
        }
        const std::string tracked = matchTrackedFile(token.text, context);
        if (!tracked.empty()) {
            details.file = tracked;
            break;
        }
        if (details.file.empty() && looksLikeFile(token.text)) {
            details.file = token.text;
        }
    }

    // Commit numbers: "3", "#3", "commit 3". A number in front of a length of
    // time is a count ("3 days ago"), not a commit number.
    for (std::size_t i = 0; i < words.size(); ++i) {
        std::string word = words[i];
        if (!word.empty() && word[0] == '#') {
            word.erase(0, 1);
        }
        if (isNumber(word)) {
            if (i + 1 < words.size() && TimeWords::isLengthOfTime(words[i + 1])) {
                continue;
            }
            const int id = std::stoi(word);
            if (id > 0 &&
                std::find(details.commitIds.begin(), details.commitIds.end(), id) == details.commitIds.end()) {
                details.commitIds.push_back(id);
            }
            continue;
        }
        // "the first commit", "the latest version".
        const bool commitNounNear = (i + 1 < words.size() && isCommitNoun(words[i + 1])) ||
                                    (i > 0 && isCommitNoun(words[i - 1]));
        if (!commitNounNear || context.commits.empty()) {
            continue;
        }
        if (word == "first" || word == "earliest" || word == "original") {
            details.commitIds.push_back(context.commits.front().id);
        } else if (word == "last" || word == "latest" || word == "newest" || word == "current") {
            details.commitIds.push_back(newestCommit(context)->id);
        }
    }

    for (int id : details.commitIds) {
        if (knownCommitId(id, context)) {
            continue;
        }
        const CommitFact* newest = newestCommit(context);
        details.problems.push_back(newest ? "There is no commit " + std::to_string(id) + ". The newest is " +
                                                std::to_string(newest->id) + "."
                                          : "There is no commit " + std::to_string(id) +
                                                " yet: nothing has been saved.");
    }

    // A time phrase stands in for a commit number when none was given. This is
    // what makes "go back to how things were yesterday evening" work.
    details.when = TimeWords::read(tokens, context.now);
    if (details.commitIds.empty() && details.when.found()) {
        bool insideWindow = false;
        const int id = commitAt(details.when, context, insideWindow);
        if (id == 0) {
            details.problems.push_back("Nothing had been saved by " + details.when.phrase +
                                       (context.commits.empty() ? ": there are no commits yet."
                                                                : ": the first commit came after that."));
        } else {
            details.commitIds.push_back(id);
            details.commitFromTime = true;
            details.usedCommitBefore = !insideWindow;
        }
    }

    // A branch the request names that doesn't exist. Only a word right next to
    // a lead-in is trusted as a name, so an unrecognised sentence says it can't
    // tell rather than guessing that some verb was the branch.
    if (intent.kind == IntentKind::Switch || intent.kind == IntentKind::Merge) {
        if (details.branch.empty()) {
            std::string guess;
            for (std::size_t i = 0; i < words.size() && guess.empty(); ++i) {
                if (isNameLeadIn(words[i]) && i + 1 < words.size() && !isNameLeadIn(words[i + 1]) &&
                    !isCommitNoun(words[i + 1]) && !isNumber(words[i + 1])) {
                    guess = words[i + 1];
                } else if (words[i] == "branch" && i > 0 && !isNameLeadIn(words[i - 1])) {
                    guess = words[i - 1];
                }
            }
            details.problems.push_back(
                (guess.empty() ? "I couldn't tell which branch you meant."
                               : "There is no branch called \"" + guess + "\".") +
                (context.branches.empty() ? "" : " The branches are " + listNames(context.branches) + "."));
        }
    } else if (intent.kind == IntentKind::CreateBranch) {
        if (!details.branch.empty()) {
            details.problems.push_back("There is already a branch called \"" + details.branch + "\".");
        } else {
            for (std::size_t i = 0; i + 1 < words.size() && details.branch.empty(); ++i) {
                if ((words[i] == "called" || words[i] == "named" || words[i] == "branch") &&
                    !isNameLeadIn(words[i + 1]) && !isNumber(words[i + 1])) {
                    details.branch = words[i + 1];
                }
            }
            if (details.branch.empty()) {
                details.problems.push_back("I couldn't tell what to call the new branch.");
            }
        }
    }

    return details;
}
