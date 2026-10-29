#include "TestHarness.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "nlp/RuleBasedParser.h"

namespace {

IntentKind kindOf(const std::string& request) {
    static const RuleBasedParser parser;
    return parser.parse(request).kind;
}

}  // namespace

// Issue #16
TEST(goBackToMondayIsARevert) {
    CHECK(kindOf("go back to Monday") == IntentKind::Revert);
    CHECK(kindOf("Go back to Monday's version, please.") == IntentKind::Revert);
}

TEST(everyCommandCanBeAskedForInPlainEnglish) {
    const std::vector<std::pair<std::string, IntentKind>> examples = {
        {"start tracking budget.csv", IntentKind::Add},
        {"add the new sheet", IntentKind::Add},
        {"save my work", IntentKind::Commit},
        {"commit this as \"fixed totals\"", IntentKind::Commit},
        {"show me the history", IntentKind::Log},
        {"list all past versions", IntentKind::Log},
        {"do I have anything to commit?", IntentKind::Status},
        {"where am I", IntentKind::Status},
        {"what changed since yesterday", IntentKind::Diff},
        {"compare commit 2 and 5", IntentKind::Diff},
        {"show commit 4", IntentKind::Show},
        {"let me see how it looked on Friday", IntentKind::Checkout},
        {"check out #3", IntentKind::Checkout},
        {"roll it back to commit 2", IntentKind::Revert},
        {"restore the version from 2 hours ago", IntentKind::Revert},
        {"create a new branch called experiment", IntentKind::CreateBranch},
        {"switch to experiment", IntentKind::Switch},
        {"I want to work on the experiment branch", IntentKind::Switch},
        {"which branches are there", IntentKind::ListBranches},
        {"merge experiment into main", IntentKind::Merge},
        {"bring in Tanishka's changes", IntentKind::Merge},
        {"cancel the merge", IntentKind::AbortMerge},
        {"undo", IntentKind::Undo},
        {"oops, take it back", IntentKind::Undo},
        {"undo my last commit", IntentKind::Undo},
        {"is the history corrupted?", IntentKind::Verify},
    };
    for (const auto& [request, expected] : examples) {
        if (kindOf(request) != expected) {
            std::cerr << "  \"" << request << "\" was read as \"" << commandName(kindOf(request)) << "\", expected \""
                      << commandName(expected) << "\"\n";
        }
        CHECK(kindOf(request) == expected);
    }
}

TEST(namingAPointInTimeTurnsUndoIntoRevert) {
    CHECK(kindOf("undo") == IntentKind::Undo);
    CHECK(kindOf("undo everything since Monday") == IntentKind::Revert);
    CHECK(kindOf("undo everything after commit 4") == IntentKind::Revert);
}

TEST(wordsInsideQuotesDontChangeTheIntent) {
    CHECK(kindOf("save it as \"merge the totals and go back to Monday\"") == IntentKind::Commit);
}

TEST(unclearRequestsAreUnknown) {
    CHECK(kindOf("") == IntentKind::Unknown);
    CHECK(kindOf("hello there") == IntentKind::Unknown);

    // Switching to a new branch could mean either command, so the parser won't guess.
    const RuleBasedParser parser;
    const Intent intent = parser.parse("switch to a new branch");
    CHECK(intent.kind == IntentKind::Unknown);
    CHECK(intent.tokens.size() == 5);
    const std::vector<Intent> ranked = parser.rank("switch to a new branch");
    CHECK(ranked.size() >= 2);
    CHECK(ranked[0].score == ranked[1].score);
}

TEST(rankListsLikelyIntentsFirstAndKeepsTheWords) {
    const RuleBasedParser parser;
    const std::vector<Intent> ranked = parser.rank("go back to Monday");
    CHECK(!ranked.empty());
    CHECK(ranked[0].kind == IntentKind::Revert);
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        CHECK(ranked[i].score > 0);
        CHECK(ranked[i].tokens.size() == 4);
        CHECK(i == 0 || ranked[i - 1].score >= ranked[i].score);
    }
    CHECK(parser.rank("hello there").empty());
}

TEST(intentsNameTheirCommand) {
    CHECK(commandName(IntentKind::Revert) == "revert");
    CHECK(commandName(IntentKind::AbortMerge) == "merge --abort");
    CHECK(commandName(IntentKind::CreateBranch) == "branch");
    CHECK(commandName(IntentKind::Unknown).empty());
}
