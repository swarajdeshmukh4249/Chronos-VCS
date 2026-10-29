#include "TestHarness.h"

#include <ctime>
#include <string>

#include "nlp/DetailExtractor.h"
#include "nlp/RuleBasedParser.h"
#include "nlp/Tokenizer.h"

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

// A made-up project, so the tests don't depend on a real repository or on the
// clock. Wednesday 28 October 2026, 14:00, with four commits:
//   1  Mon 26th 09:00   2  Tue 27th 18:30   3  Tue 27th 20:00   4  Wed 28th 11:00
// Commits 2 and 3 are the ones made "yesterday evening".
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

// Details for a request whose command is already known, so these test the
// detail extraction on its own.
Details detailsOf(const std::string& request, IntentKind kind) {
    Intent intent;
    intent.kind = kind;
    intent.tokens = Tokenizer::tokenize(request);
    return DetailExtractor::extract(intent, project());
}

// Details for a request parsed from scratch, command and all.
Details parsed(const std::string& request) {
    static const RuleBasedParser parser;
    return DetailExtractor::extract(parser.parse(request), project());
}

}  // namespace

// Issue #17: "yesterday evening" has to come out as a real commit number.
TEST(yesterdayEveningResolvesToACommit) {
    const Details details = parsed("go back to how things were yesterday evening");
    CHECK(details.resolved());
    CHECK(details.namesCommit());
    // Commits 2 and 3 were both saved that evening; the newest one is how
    // things stood by the end of it.
    CHECK(details.commitId() == 3);
    CHECK(details.commitFromTime);
    CHECK(!details.usedCommitBefore);
    CHECK(details.when.phrase == "yesterday evening");
}

TEST(aTimeWithNothingSavedInItFallsBackToTheVersionInPlace) {
    // Nothing was saved on the morning of the 27th, so the version in place
    // then was commit 1, from the day before.
    const Details details = detailsOf("restore how it looked yesterday morning", IntentKind::Revert);
    CHECK(details.resolved());
    CHECK(details.commitId() == 1);
    CHECK(details.commitFromTime);
    CHECK(details.usedCommitBefore);
}

TEST(aTimeFromBeforeTheProjectStartedIsRefused) {
    const Details details = detailsOf("go back to 3 days ago", IntentKind::Revert);
    CHECK(!details.resolved());
    CHECK(!details.namesCommit());
    CHECK(details.problems.size() == 1);
    CHECK(details.problems.front() == "Nothing had been saved by 3 days ago: the first commit came after that.");
}

TEST(aCommitNumberIsTakenAsGiven) {
    const Details numbered = detailsOf("roll it back to commit 2", IntentKind::Revert);
    CHECK(numbered.resolved());
    CHECK(numbered.commitId() == 2);
    CHECK(!numbered.commitFromTime);

    CHECK(detailsOf("revert 3", IntentKind::Revert).commitId() == 3);
    CHECK(detailsOf("check out #3", IntentKind::Checkout).commitId() == 3);
}

TEST(aCountedLengthOfTimeIsNotACommitNumber) {
    // "3 days ago" must not be read as commit 3. Here it lands on the 25th,
    // before the project started, so the request is refused rather than
    // quietly reverting to commit 3.
    const Details details = detailsOf("go back 3 days", IntentKind::Revert);
    CHECK(!details.namesCommit());
    CHECK(!details.resolved());
}

TEST(aCommitNumberPastTheEndOfHistoryIsRefused) {
    const Details details = detailsOf("revert to commit 9", IntentKind::Revert);
    CHECK(!details.resolved());
    CHECK(details.problems.front() == "There is no commit 9. The newest is 4.");
}

TEST(theFirstAndLatestCommitsCanBeAskedForByName) {
    CHECK(detailsOf("show me the first commit", IntentKind::Show).commitId() == 1);
    CHECK(detailsOf("show the latest version", IntentKind::Show).commitId() == 4);
}

TEST(aDiffCanNameTwoCommits) {
    const Details details = detailsOf("compare commit 2 and 4", IntentKind::Diff);
    CHECK(details.resolved());
    CHECK(details.commitIds.size() == 2);
    CHECK(details.commitIds[0] == 2);
    CHECK(details.commitIds[1] == 4);
}

TEST(anExistingBranchIsFoundWhateverTheCase) {
    CHECK(detailsOf("switch to experiment", IntentKind::Switch).branch == "experiment");
    CHECK(detailsOf("I want to work on the Experiment branch", IntentKind::Switch).branch == "experiment");
    CHECK(detailsOf("switch to EXPERIMENT", IntentKind::Switch).resolved());
}

TEST(aMergeTakesTheBranchThatIsNotTheCurrentOne) {
    // Both names are branches, so word order isn't what decides it: we are on
    // main, so the branch being merged in must be experiment.
    const Details details = detailsOf("merge experiment into main", IntentKind::Merge);
    CHECK(details.resolved());
    CHECK(details.branch == "experiment");

    CHECK(detailsOf("merge experiment", IntentKind::Merge).branch == "experiment");
}

TEST(aBranchThatDoesNotExistIsNamedInTheProblem) {
    const Details details = detailsOf("switch to nowhere", IntentKind::Switch);
    CHECK(!details.resolved());
    CHECK(details.branch.empty());
    CHECK(details.problems.front() == "There is no branch called \"nowhere\". The branches are main and experiment.");
}

TEST(anUnreadableBranchRequestSaysSoRatherThanGuessing) {
    // Nothing here looks like a name next to a lead-in word, so it says it
    // can't tell instead of deciding some verb was the branch.
    const Details details = detailsOf("switch over", IntentKind::Switch);
    CHECK(!details.resolved());
    CHECK(details.problems.front() == "I couldn't tell which branch you meant. The branches are main and experiment.");
}

TEST(aNewBranchNameIsPickedUpFromCalledOrNamed) {
    CHECK(detailsOf("create a new branch called fix-totals", IntentKind::CreateBranch).branch == "fix-totals");
    CHECK(detailsOf("make a branch named demo", IntentKind::CreateBranch).branch == "demo");
    CHECK(detailsOf("create a branch called fix-totals", IntentKind::CreateBranch).resolved());
}

TEST(creatingABranchThatAlreadyExistsIsRefused) {
    const Details details = detailsOf("create a branch called experiment", IntentKind::CreateBranch);
    CHECK(!details.resolved());
    CHECK(details.problems.front() == "There is already a branch called \"experiment\".");
}

TEST(aNamelessNewBranchIsRefused) {
    const Details details = detailsOf("make me a new branch", IntentKind::CreateBranch);
    CHECK(!details.resolved());
    CHECK(details.problems.front() == "I couldn't tell what to call the new branch.");
}

TEST(theQuotedPartOfARequestIsTheCommitMessage) {
    const Details details = parsed("commit this as \"fixed totals\"");
    CHECK(details.message == "fixed totals");
    // A quoted message is never mistaken for a file or a branch name.
    CHECK(details.file.empty());
    CHECK(details.branch.empty());
}

TEST(aQuotedMessageIsNotSearchedForTimesOrNumbers) {
    const Details details = detailsOf("save it as \"notes.txt tidy-up, 3 days of work\"", IntentKind::Commit);
    CHECK(details.message == "notes.txt tidy-up, 3 days of work");
    CHECK(details.file.empty());
    CHECK(!details.namesCommit());
}

TEST(aTrackedFileIsFoundByNameOrByPath) {
    CHECK(detailsOf("what changed in budget.csv", IntentKind::Diff).file == "budget.csv");
    // Matched whatever case it was typed in, and given back as tracked.
    CHECK(detailsOf("add Budget.CSV", IntentKind::Add).file == "budget.csv");
    CHECK(detailsOf("show me notes.txt", IntentKind::Show).file == "notes.txt");
}

TEST(aFileThatIsNotTrackedYetIsStillRecognised) {
    // "add" is how a file becomes tracked, so it can't be checked against the
    // tracked list; it only has to look like a path.
    CHECK(detailsOf("start tracking new-sheet.csv", IntentKind::Add).file == "new-sheet.csv");
    CHECK(detailsOf("add data/q3.csv", IntentKind::Add).file == "data/q3.csv");
}

TEST(ordinaryWordsAreNotMistakenForFiles) {
    CHECK(detailsOf("save my work", IntentKind::Commit).file.empty());
    CHECK(detailsOf("show me the history", IntentKind::Log).file.empty());
    CHECK(detailsOf("check out 2026-10-20", IntentKind::Checkout).file.empty());
}

TEST(aRequestNamingNothingLeavesEverythingEmpty) {
    const Details details = parsed("save my work");
    CHECK(details.resolved());
    CHECK(details.branch.empty());
    CHECK(details.file.empty());
    CHECK(details.message.empty());
    CHECK(!details.namesCommit());
    CHECK(!details.when.found());
}

TEST(commitAtPicksTheNewestCommitInsideTheWindowOrTheOneBefore) {
    const RequestContext context = project();
    bool insideWindow = false;

    const TimeWindow evening = TimeWords::read(Tokenizer::tokenize("yesterday evening"), context.now);
    CHECK(DetailExtractor::commitAt(evening, context, insideWindow) == 3);
    CHECK(insideWindow);

    const TimeWindow morning = TimeWords::read(Tokenizer::tokenize("yesterday morning"), context.now);
    CHECK(DetailExtractor::commitAt(morning, context, insideWindow) == 1);
    CHECK(!insideWindow);

    // Earlier than the whole history, so there is nothing to point at.
    const TimeWindow tooEarly = TimeWords::read(Tokenizer::tokenize("2026-10-01"), context.now);
    CHECK(DetailExtractor::commitAt(tooEarly, context, insideWindow) == 0);
    CHECK(!insideWindow);
}

TEST(detailsSurviveTheWholeJourneyFromPlainEnglish) {
    // The demo line, start to finish: words in, command and commit number out.
    static const RuleBasedParser parser;
    const Intent intent = parser.parse("go back to how things were yesterday evening");
    CHECK(intent.kind == IntentKind::Revert);
    CHECK(commandName(intent.kind) == "revert");

    const Details details = DetailExtractor::extract(intent, project());
    CHECK(details.resolved());
    CHECK(details.commitId() == 3);
}
