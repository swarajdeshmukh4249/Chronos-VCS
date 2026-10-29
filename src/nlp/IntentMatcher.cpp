#include "nlp/IntentMatcher.h"

#include <algorithm>
#include <cctype>
#include <set>

std::string commandName(IntentKind kind) {
    switch (kind) {
        case IntentKind::Add: return "add";
        case IntentKind::Commit: return "commit";
        case IntentKind::Log: return "log";
        case IntentKind::Status: return "status";
        case IntentKind::Diff: return "diff";
        case IntentKind::Show: return "show";
        case IntentKind::Checkout: return "checkout";
        case IntentKind::Revert: return "revert";
        case IntentKind::CreateBranch: return "branch";
        case IntentKind::Switch: return "switch";
        case IntentKind::ListBranches: return "branch";
        case IntentKind::Merge: return "merge";
        case IntentKind::AbortMerge: return "merge --abort";
        case IntentKind::Undo: return "undo";
        case IntentKind::Verify: return "verify";
        case IntentKind::Unknown: break;
    }
    return "";
}

std::vector<std::string> IntentMatcher::keyWords(const std::vector<Token>& tokens) {
    static const std::set<std::string> fillers = {"a",  "an",  "the",    "my", "our", "your", "this", "that",
                                                  "it", "its", "please", "me", "us",  "just", "kindly"};
    std::vector<std::string> words;
    for (const Token& token : tokens) {
        if (!token.quoted && fillers.count(token.word) == 0) {
            words.push_back(token.word);
        }
    }
    return words;
}

PhraseMatcher::PhraseMatcher(IntentKind kind, const std::vector<Phrase>& phrases) : IntentMatcher(kind) {
    // Phrases go through the same steps as requests, so "take it back" is stored as take, back.
    for (const Phrase& phrase : phrases) {
        phrases_.emplace_back(keyWords(Tokenizer::tokenize(phrase.words)), phrase.weight);
    }
}

int PhraseMatcher::score(const std::vector<Token>& tokens) const {
    const std::vector<std::string> words = keyWords(tokens);
    int total = 0;
    for (const auto& [phrase, weight] : phrases_) {
        if (!phrase.empty() && std::search(words.begin(), words.end(), phrase.begin(), phrase.end()) != words.end()) {
            total += weight;
        }
    }
    return total;
}

namespace {

// "12", "#12", "2026-10-20", "20/10", "17:30", "5pm".
bool isNumberOrDate(std::string word) {
    if (!word.empty() && word[0] == '#') {
        word.erase(0, 1);
    }
    if (word.size() > 2 && (word.compare(word.size() - 2, 2, "am") == 0 || word.compare(word.size() - 2, 2, "pm") == 0)) {
        word.resize(word.size() - 2);
    }
    const bool hasDigit = std::any_of(word.begin(), word.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
    const bool onlyDateCharacters = std::all_of(word.begin(), word.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '/' || c == ':';
    });
    return hasDigit && onlyDateCharacters;
}

}  // namespace

bool PointInTimeMatcher::namesPointInTime(const std::vector<Token>& tokens) {
    static const std::set<std::string> timeWords = {
        "monday", "tuesday", "wednesday", "thursday",  "friday",  "saturday", "sunday", "yesterday", "today",
        "tonight", "morning", "afternoon", "evening",  "night",   "ago",      "earlier", "minute",   "minutes",
        "hour",   "hours",   "day",       "days",      "week",    "weeks",    "month",  "months",
    };
    return std::any_of(tokens.begin(), tokens.end(), [](const Token& token) {
        return !token.quoted && (timeWords.count(token.word) > 0 || isNumberOrDate(token.word));
    });
}

int PointInTimeMatcher::score(const std::vector<Token>& tokens) const {
    const int words = PhraseMatcher::score(tokens);
    return words > 0 && namesPointInTime(tokens) ? words + 2 : words;
}

int UndoMatcher::score(const std::vector<Token>& tokens) const {
    const int words = PhraseMatcher::score(tokens);
    return PointInTimeMatcher::namesPointInTime(tokens) ? std::min(words, 1) : words;
}
