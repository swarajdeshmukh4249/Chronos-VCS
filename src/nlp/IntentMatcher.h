#pragma once

#include <string>
#include <utility>
#include <vector>

#include "nlp/Intent.h"
#include "nlp/Tokenizer.h"

// Scores how strongly a request asks for one kind of intent. The parser asks
// every matcher and keeps the highest score.
class IntentMatcher {
public:
    explicit IntentMatcher(IntentKind kind) : kind_(kind) {}
    virtual ~IntentMatcher() = default;

    IntentKind kind() const { return kind_; }

    // 0 means the request isn't asking for this at all.
    virtual int score(const std::vector<Token>& tokens) const = 0;

    // The unquoted words that carry meaning, without fillers such as "the",
    // "my" or "please": "go back to the Monday version" -> go back to monday version.
    static std::vector<std::string> keyWords(const std::vector<Token>& tokens);

private:
    IntentKind kind_;
};

// Words that suggest an intent, with how strongly. Several words must appear
// next to each other once fillers are taken out ("roll it back" matches "roll back").
struct Phrase {
    std::string words;
    int weight;
};

// Adds up the weights of the phrases that appear in the request.
class PhraseMatcher : public IntentMatcher {
public:
    PhraseMatcher(IntentKind kind, const std::vector<Phrase>& phrases);
    int score(const std::vector<Token>& tokens) const override;

private:
    std::vector<std::pair<std::vector<std::string>, int>> phrases_;
};

// For intents that go to a point in history (revert and checkout): naming a
// day, a time or a commit number makes the request more likely to mean them.
class PointInTimeMatcher : public PhraseMatcher {
public:
    using PhraseMatcher::PhraseMatcher;
    int score(const std::vector<Token>& tokens) const override;

    // "Monday", "yesterday", "2 hours ago", "#12", "3", "2026-10-20", "5pm".
    static bool namesPointInTime(const std::vector<Token>& tokens);
};

// "undo" alone takes back the last command, but "undo everything since Monday"
// is a revert, so naming a point in time weakens this match.
class UndoMatcher : public PhraseMatcher {
public:
    explicit UndoMatcher(const std::vector<Phrase>& phrases) : PhraseMatcher(IntentKind::Undo, phrases) {}
    int score(const std::vector<Token>& tokens) const override;
};
