#include "nlp/RuleBasedParser.h"

#include <algorithm>

#include "nlp/Tokenizer.h"

Intent IntentParser::parse(const std::string& request) const {
    std::vector<Intent> ranked = rank(request);
    if (ranked.empty()) {
        return Intent{IntentKind::Unknown, 0, Tokenizer::tokenize(request)};
    }
    if (ranked.size() > 1 && ranked[1].score == ranked[0].score) {
        return Intent{IntentKind::Unknown, 0, ranked[0].tokens};
    }
    return ranked[0];
}

// Weights: 3 for words that name the command on their own, 2 for strong hints,
// 1 for weak ones. Where two commands share a word, the more specific phrase
// carries the extra weight, e.g. "cancel the merge" beats plain "merge".
RuleBasedParser::RuleBasedParser() {
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::Add, std::vector<Phrase>{{"add", 3}, {"track", 3}, {"start tracking", 3}, {"include", 2}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::Commit,
        std::vector<Phrase>{{"commit", 2}, {"save", 3}, {"snapshot", 2}, {"checkpoint", 2}, {"record", 2}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::Log, std::vector<Phrase>{{"log", 3},
                                             {"history", 3},
                                             {"past versions", 3},
                                             {"previous versions", 3},
                                             {"list commits", 3},
                                             {"all commits", 3}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::Status, std::vector<Phrase>{{"status", 3},
                                                {"uncommitted", 3},
                                                {"unsaved", 3},
                                                {"where am i", 3},
                                                {"branch am i on", 3},
                                                {"anything to commit", 3},
                                                {"anything to save", 3}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::Diff, std::vector<Phrase>{{"diff", 3},
                                              {"difference", 3},
                                              {"differences", 3},
                                              {"different", 3},
                                              {"compare", 3},
                                              {"what changed", 3},
                                              {"show changes", 3},
                                              {"changed", 1},
                                              {"changes", 1}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::Show, std::vector<Phrase>{{"show commit", 3}, {"details", 2}, {"tell me about", 2}, {"show", 1}}));
    matchers_.push_back(std::make_unique<PointInTimeMatcher>(
        IntentKind::Checkout, std::vector<Phrase>{{"checkout", 3},
                                                  {"check out", 3},
                                                  {"how it looked", 3},
                                                  {"what it looked like", 3},
                                                  {"how it was", 3},
                                                  {"look at", 2},
                                                  {"take a look", 2},
                                                  {"peek", 2}}));
    matchers_.push_back(std::make_unique<PointInTimeMatcher>(
        IntentKind::Revert, std::vector<Phrase>{{"revert", 3},
                                                {"go back", 3},
                                                {"roll back", 3},
                                                {"rollback", 3},
                                                {"undo everything", 3},
                                                {"back to", 2},
                                                {"return to", 2},
                                                {"restore", 2},
                                                {"put back", 2},
                                                {"everything since", 2}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::CreateBranch, std::vector<Phrase>{{"new branch", 3},
                                                      {"create branch", 3},
                                                      {"make branch", 3},
                                                      {"start branch", 3},
                                                      {"branch off", 3},
                                                      {"branch called", 2},
                                                      {"create", 1}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::Switch, std::vector<Phrase>{{"switch", 3},
                                                {"work on", 3},
                                                {"move to", 2},
                                                {"change to", 2},
                                                {"jump to", 2},
                                                {"go to", 2}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::ListBranches,
        std::vector<Phrase>{{"branches", 3}, {"list branches", 2}, {"which branches", 2}, {"what branches", 2}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::Merge, std::vector<Phrase>{{"merge", 3}, {"combine", 3}, {"bring", 2}, {"pull", 2}, {"join", 2}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        IntentKind::AbortMerge, std::vector<Phrase>{{"abort", 4},
                                                    {"cancel", 3},
                                                    {"cancel merge", 2},
                                                    {"stop merge", 4},
                                                    {"stop merging", 4},
                                                    {"give up on merge", 4}}));
    matchers_.push_back(std::make_unique<UndoMatcher>(
        std::vector<Phrase>{{"undo", 3}, {"undo last", 2}, {"cancel last", 4}, {"take it back", 3}, {"oops", 2}}));
    matchers_.push_back(std::make_unique<PhraseMatcher>(
        // Above "history" for Log, so "is the history corrupted?" is a check.
        IntentKind::Verify, std::vector<Phrase>{{"verify", 3},
                                                {"corrupt", 4},
                                                {"corrupted", 4},
                                                {"damaged", 4},
                                                {"intact", 4},
                                                {"integrity", 4}}));
}

std::vector<Intent> RuleBasedParser::rank(const std::string& request) const {
    const std::vector<Token> tokens = Tokenizer::tokenize(request);
    std::vector<Intent> ranked;
    for (const auto& matcher : matchers_) {
        if (const int score = matcher->score(tokens); score > 0) {
            ranked.push_back(Intent{matcher->kind(), score, tokens});
        }
    }
    // Stable, so equal scores keep the order above and parse() can spot the tie.
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const Intent& a, const Intent& b) { return a.score > b.score; });
    return ranked;
}
