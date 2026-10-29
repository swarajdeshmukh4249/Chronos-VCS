#include "TestHarness.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

#include "nlp/RequestPlanner.h"
#include "nlp/RuleBasedParser.h"

namespace {

std::time_t at(int year, int month, int day, int hour, int minute = 0) {
    std::tm parts{};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_isdst = -1;
    return std::mktime(&parts);
}

// The same made-up project the detail tests use: Wednesday 28 October 2026 at
// 14:00, four commits, nothing uncommitted and no merge waiting.
RequestContext project() {
    RequestContext context;
    context.branches = {"main", "experiment"};
    context.currentBranch = "main";
    context.trackedFiles = {"budget.csv", "notes.txt"};
    context.commits = {
        {1, at(2026, 10, 26, 9, 0), "first draft"},
        {2, at(2026, 10, 27, 18, 30), "added Q3"},
        {3, at(2026, 10, 27, 20, 0), "fixed totals"},
        {4, at(2026, 10, 28, 11, 0), "renamed columns"},
    };
    context.now = at(2026, 10, 28, 14, 0);
    return context;
}

RequestPlan planFor(const std::string& request, const RequestContext& context) {
    static const RuleBasedParser parser;
    static const RequestPlanner planner(parser);
    return planner.plan(request, context);
}

RequestPlan planFor(const std::string& request) {
    return planFor(request, project());
}

bool argsAre(const RequestPlan& plan, const std::vector<std::string>& expected) {
    return plan.args == expected;
}

bool anyNoteContains(const RequestPlan& plan, const std::string& text) {
    return std::any_of(plan.notes.begin(), plan.notes.end(),
                       [&text](const std::string& note) { return note.find(text) != std::string::npos; });
}

bool anyProblemContains(const RequestPlan& plan, const std::string& text) {
    return std::any_of(plan.problems.begin(), plan.problems.end(),
                       [&text](const std::string& problem) { return problem.find(text) != std::string::npos; });
}

bool anyOptionContains(const RequestPlan& plan, const std::string& text) {
    return std::any_of(plan.options.begin(), plan.options.end(),
                       [&text](const std::string& option) { return option.find(text) != std::string::npos; });
}

}  // namespace

// Issue #18: the done-when. Everything that can overwrite or rewind work has
// to ask before it happens.
TEST(everyDestructiveRequestAsksFirst) {
    RequestContext context = project();
    context.isMerging = true;
    context.undoable = "revert to commit 2";

    for (const std::string& request : {std::string("go back to how things were yesterday evening"),
                                       std::string("roll it back to commit 2"),
                                       std::string("check out #3"),
                                       std::string("undo that"),
                                       std::string("cancel the merge")}) {
        const RequestPlan plan = planFor(request, context);
        CHECK(plan.understood());
        CHECK(plan.outcome == RequestPlan::Outcome::Confirm);
        // A confirmation with no question would be a prompt with nothing in it.
        CHECK(!plan.question.empty());
        CHECK(!plan.action.empty());
    }
}

TEST(harmlessRequestsAreNotQueried) {
    for (const std::string& request : {std::string("show me the history"), std::string("what changed today"),
                                       std::string("where am I"), std::string("which branches are there"),
                                       std::string("show commit 2"), std::string("check the saved history"),
                                       std::string("commit this as \"fixed totals\""),
                                       std::string("create a new branch called fix-totals")}) {
        const RequestPlan plan = planFor(request);
        CHECK(plan.understood());
        CHECK(plan.outcome == RequestPlan::Outcome::Run);
        CHECK(plan.question.empty());
    }
}

TEST(theFourCommandsThatCanLoseWorkAreTheOnesMarked) {
    CHECK(RequestPlanner::canLoseWork(IntentKind::Checkout));
    CHECK(RequestPlanner::canLoseWork(IntentKind::Revert));
    CHECK(RequestPlanner::canLoseWork(IntentKind::Undo));
    CHECK(RequestPlanner::canLoseWork(IntentKind::AbortMerge));
    // These write files too, but only uncommitted work is ever at risk.
    CHECK(!RequestPlanner::canLoseWork(IntentKind::Switch));
    CHECK(!RequestPlanner::canLoseWork(IntentKind::Merge));
    CHECK(!RequestPlanner::canLoseWork(IntentKind::Commit));
    CHECK(!RequestPlanner::canLoseWork(IntentKind::Log));
}

TEST(switchingAsksOnlyWhenThereIsUncommittedWork) {
    const RequestPlan quiet = planFor("switch to experiment");
    CHECK(quiet.outcome == RequestPlan::Outcome::Run);

    RequestContext dirty = project();
    dirty.hasUncommittedChanges = true;
    const RequestPlan risky = planFor("switch to experiment", dirty);
    CHECK(risky.outcome == RequestPlan::Outcome::Confirm);
    CHECK(risky.question.find("no commit holds yet") != std::string::npos);
}

TEST(aRequestBecomesOrdinaryCommandLineWords) {
    // The plain-English layer adds no commands of its own: it comes back out
    // as the words a person could have typed.
    CHECK(argsAre(planFor("go back to how things were yesterday evening"), {"revert", "3"}));
    CHECK(argsAre(planFor("check out #3"), {"checkout", "3"}));
    CHECK(argsAre(planFor("commit this as \"fixed totals\""), {"commit", "-m", "fixed totals"}));
    CHECK(argsAre(planFor("start tracking new-sheet.csv"), {"add", "new-sheet.csv"}));
    CHECK(argsAre(planFor("show me the history"), {"log"}));
    CHECK(argsAre(planFor("list all past versions"), {"log", "--all"}));
    CHECK(argsAre(planFor("which branches are there"), {"branch"}));
    CHECK(argsAre(planFor("create a new branch called fix-totals"), {"branch", "fix-totals"}));
    CHECK(argsAre(planFor("switch to experiment"), {"switch", "experiment"}));
    CHECK(argsAre(planFor("where am I"), {"status"}));
    CHECK(argsAre(planFor("compare commit 2 and 4"), {"diff", "2", "4"}));
    CHECK(argsAre(planFor("show commit 2"), {"show", "2"}));
    CHECK(argsAre(planFor("check that no history was damaged"), {"verify"}));
}

TEST(wordsThatPointAtTwoCommandsEquallyAreAskedAbout) {
    // "verify the history" names verify and log just as strongly, so the
    // parser refuses to pick on a coin flip and the reading is offered back.
    const RequestPlan plan = planFor("verify the history");
    CHECK(plan.outcome == RequestPlan::Outcome::DidYouMean);
    CHECK(plan.options.size() == 2);
    CHECK(anyOptionContains(plan, "chrono verify"));
    CHECK(anyOptionContains(plan, "chrono log"));
    CHECK(plan.args.empty());
}

TEST(theRequestSaysWhichCommitATimePhraseLandedOn) {
    const RequestPlan plan = planFor("go back to how things were yesterday evening");
    CHECK(plan.outcome == RequestPlan::Outcome::Confirm);
    // Shown before the question, so a wrong reading is caught while the files
    // are still untouched.
    CHECK(anyNoteContains(plan, "yesterday evening"));
    CHECK(anyNoteContains(plan, "commit 3"));
    CHECK(anyNoteContains(plan, "fixed totals"));
}

TEST(fallingBackToAnEarlierCommitIsSaidOutLoud) {
    // Nothing was saved on the morning of the 27th, so this lands on commit 1.
    const RequestPlan plan = planFor("restore the version from yesterday morning");
    CHECK(plan.understood());
    CHECK(argsAre(plan, {"revert", "1"}));
    CHECK(anyNoteContains(plan, "Nothing was saved during yesterday morning"));
    CHECK(anyNoteContains(plan, "the version in place at the time"));
}

TEST(aCommitNumberNeedsNoExplanation) {
    const RequestPlan plan = planFor("roll it back to commit 2");
    CHECK(argsAre(plan, {"revert", "2"}));
    // Nothing was interpreted, so there is nothing to explain.
    CHECK(plan.notes.empty());
}

TEST(aNearMissBranchNameIsOfferedBack) {
    const RequestPlan plan = planFor("switch to experiment");
    CHECK(plan.understood());

    const RequestPlan misspelled = planFor("switch to expiriment");
    CHECK(misspelled.outcome == RequestPlan::Outcome::DidYouMean);
    CHECK(anyOptionContains(misspelled, "experiment"));
    // Offered, never acted on.
    CHECK(misspelled.args.empty());
}

TEST(aNameNothingLikeABranchIsRefusedRatherThanGuessedAt) {
    const RequestPlan plan = planFor("switch to accounting");
    CHECK(plan.outcome == RequestPlan::Outcome::Unclear);
    CHECK(anyProblemContains(plan, "There is no branch called \"accounting\""));
    CHECK(anyProblemContains(plan, "main and experiment"));
    CHECK(plan.args.empty());
}

TEST(aRequestThatMeansNothingSaysSoAndSuggestsWhatToTry) {
    const RequestPlan plan = planFor("zxcvbnm qwertyuiop");
    CHECK(plan.outcome == RequestPlan::Outcome::Unclear);
    CHECK(anyProblemContains(plan, "I didn't understand"));
    CHECK(anyProblemContains(plan, "go back to yesterday evening"));
    CHECK(plan.args.empty());
}

TEST(nothingRunsUnlessItWasUnderstood) {
    for (const std::string& request : {std::string("zxcvbnm qwertyuiop"), std::string("switch to accounting"),
                                       std::string("save my work"), std::string("check it out")}) {
        const RequestPlan plan = planFor(request);
        CHECK(!plan.understood());
        CHECK(plan.args.empty());
    }
}

TEST(aSaveWithNoMessageAsksForOneInsteadOfInventingIt) {
    const RequestPlan plan = planFor("save my work");
    CHECK(plan.outcome == RequestPlan::Outcome::Unclear);
    CHECK(anyProblemContains(plan, "I don't know what to call this version"));
    CHECK(plan.args.empty());
}

TEST(aTimeFromBeforeTheProjectStartedIsRefusedNotRoundedOff) {
    const RequestPlan plan = planFor("go back to 3 days ago");
    CHECK(plan.outcome == RequestPlan::Outcome::Unclear);
    CHECK(anyProblemContains(plan, "Nothing had been saved by 3 days ago"));
    // The dangerous failure would be quietly reverting to commit 3 instead.
    CHECK(plan.args.empty());
}

TEST(cancellingAMergeWhenNoneIsWaitingIsRefused) {
    const RequestPlan plan = planFor("cancel the merge");
    CHECK(plan.outcome == RequestPlan::Outcome::Unclear);
    CHECK(anyProblemContains(plan, "no merge waiting"));

    RequestContext merging = project();
    merging.isMerging = true;
    const RequestPlan waiting = planFor("cancel the merge", merging);
    CHECK(waiting.outcome == RequestPlan::Outcome::Confirm);
    CHECK(argsAre(waiting, {"merge", "--abort"}));
    CHECK(waiting.question.find("throws away") != std::string::npos);
}

TEST(undoWithNothingToUndoIsRefused) {
    const RequestPlan plan = planFor("undo that");
    CHECK(plan.outcome == RequestPlan::Outcome::Unclear);
    CHECK(anyProblemContains(plan, "nothing to undo"));

    RequestContext undoable = project();
    undoable.undoable = "revert to commit 2";
    const RequestPlan ready = planFor("undo that", undoable);
    CHECK(ready.outcome == RequestPlan::Outcome::Confirm);
    CHECK(argsAre(ready, {"undo"}));
    CHECK(ready.action.find("revert to commit 2") != std::string::npos);
}

TEST(everyCommandHasPlainWordsToOfferBack) {
    for (const IntentKind kind : {IntentKind::Add, IntentKind::Commit, IntentKind::Log, IntentKind::Status,
                                  IntentKind::Diff, IntentKind::Show, IntentKind::Checkout, IntentKind::Revert,
                                  IntentKind::CreateBranch, IntentKind::Switch, IntentKind::ListBranches,
                                  IntentKind::Merge, IntentKind::AbortMerge, IntentKind::Undo, IntentKind::Verify}) {
        CHECK(!RequestPlanner::plainDescription(kind).empty());
    }
    CHECK(RequestPlanner::plainDescription(IntentKind::Unknown).empty());
}

TEST(mergingNamesBothBranchesInWhatItSaysItWillDo) {
    const RequestPlan plan = planFor("merge experiment into main");
    CHECK(plan.understood());
    CHECK(argsAre(plan, {"merge", "experiment"}));
    CHECK(plan.action.find("experiment") != std::string::npos);
    CHECK(plan.action.find("main") != std::string::npos);
}

TEST(theDemoLineWorksEndToEnd) {
    // What gets typed on stage, and what has to come out of it.
    const RequestPlan plan = planFor("go back to how things were yesterday evening");
    CHECK(plan.outcome == RequestPlan::Outcome::Confirm);
    CHECK(argsAre(plan, {"revert", "3"}));
    CHECK(plan.action == "put the files from commit 3 back and save that as a new commit");
    CHECK(anyNoteContains(plan, "By yesterday evening you mean commit 3"));
    CHECK(plan.question == "Go ahead?");
}
