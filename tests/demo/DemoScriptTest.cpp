#include "TestHarness.h"

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "TempDir.h"
#include "common/Exceptions.h"
#include "core/Command.h"
#include "core/Repository.h"
#include "core/StorageManager.h"
#include "nlp/RequestPlanner.h"
#include "nlp/RuleBasedParser.h"
#include "sheet/CSVHandler.h"
#include "sheet/SheetEvaluator.h"

// Issue #37: the demo script, start to finish, in the order it is performed.
// Each step of the plan in PROJECT_CONTEXT section 8 is one test here, so a
// failure says which part of the demo just broke rather than only that
// something did.

namespace fs = std::filesystem;

namespace {

const char* const kAuthor = "Swaraj";

// A fresh repository in a folder that cleans itself up.
struct Project {
    TempDir dir;
    Repository repo;
    WorkingTree tree;

    Project() : repo(Repository::init(dir.path())), tree(dir.path()) {}

    // Runs a command the way the command line does, so `undo` can take it back.
    std::string run(const std::vector<std::string>& args, int expectedCode = 0) {
        std::ostringstream out;
        const std::unique_ptr<Command> command = CommandFactory::create(args, kAuthor);
        CHECK(command != nullptr);
        CHECK(command->run(repo, out) == expectedCode);
        return out.str();
    }
};

std::string sheet(const CSVHandler::Rows& rows) {
    return CSVHandler::formatRows(rows);
}

// What a cell comes out as once the sheet is evaluated.
std::string valueOf(const std::string& csv, const std::string& cell) {
    const Grid<Cell> grid = CSVHandler::parse(csv);
    const SheetEvaluator evaluator(grid);
    return valueToString(evaluator.evaluate(cell));
}

// The picture of the repository the plain-English layer reads. This mirrors
// contextFor() in main.cpp; the demo types its requests at the command line.
RequestContext contextFor(const Repository& repo) {
    RequestContext context;
    for (const Branch& branch : repo.getBranches()) {
        context.branches.push_back(branch.getName());
    }
    context.currentBranch = repo.getCurrentBranch();
    context.trackedFiles.assign(repo.getTracked().begin(), repo.getTracked().end());
    for (const Commit& commit : repo.log()) {
        context.commits.push_back({commit.getId(), commit.getTimestamp(), commit.getMessage()});
    }
    context.now = std::time(nullptr);
    context.hasUncommittedChanges = repo.hasUncommittedChanges();
    context.isMerging = repo.isMerging();
    context.undoable = repo.nextUndo();
    return context;
}

// Three commits of a small sheet: the state the demo starts from.
void threeCommits(Project& project) {
    project.tree.write("budget.csv", sheet({{"Item", "Q1"}, {"Rent", "1200"}}));
    project.repo.track("budget.csv");
    project.repo.commit("first draft", kAuthor);
    project.tree.write("budget.csv", sheet({{"Item", "Q1"}, {"Rent", "1250"}}));
    project.repo.commit("rent went up", kAuthor);
    project.tree.write("budget.csv", sheet({{"Item", "Q1"}, {"Rent", "1300"}}));
    project.repo.commit("rent went up again", kAuthor);
}

}  // namespace

// Demo step 1: init, three commits, log, diff.
TEST(step1_threeCommitsThenLogAndDiff) {
    Project project;
    threeCommits(project);

    CHECK(project.repo.log().size() == 3);
    CHECK(project.repo.getHead() == 3);
    CHECK(project.repo.historyOfHead().size() == 3);
    CHECK(project.repo.getCommit(1).getMessage() == "first draft");
    CHECK(!project.repo.hasUncommittedChanges());

    // A .csv is compared cell by cell (#15), so the diff names the cell that
    // changed rather than the whole line.
    const std::vector<FileDiff> diffs = project.repo.diff(1, 3);
    CHECK(diffs.size() == 1);
    CHECK(diffs.front().path == "budget.csv");
    CHECK(diffs.front().result.changes.size() == 1);
    CHECK(diffs.front().result.changes.front().location == "B2");
    CHECK(diffs.front().result.changes.front().before == "1200");
    CHECK(diffs.front().result.changes.front().after == "1300");
}

// Demo step 2: revert to commit 1, see the file change back, then undo it.
TEST(step2_revertPutsTheOldFileBackAndUndoTakesItAway) {
    Project project;
    threeCommits(project);
    const std::string newest = project.tree.read("budget.csv");

    project.run({"revert", "1"});
    CHECK(project.tree.read("budget.csv") == sheet({{"Item", "Q1"}, {"Rent", "1200"}}));
    // The revert is itself a commit, so nothing is lost by going back.
    CHECK(project.repo.log().size() == 4);
    CHECK(project.repo.getHead() == 4);
    CHECK(project.repo.nextUndo().find("revert") != std::string::npos);

    project.run({"undo"});
    CHECK(project.tree.read("budget.csv") == newest);
    CHECK(project.repo.getHead() == 3);
    // Undo moves HEAD and the files back, but deliberately keeps the commit it
    // undid: nothing is ever deleted from history, and `log --all` still shows
    // it. So there are still four commits.
    CHECK(project.repo.log().size() == 4);
}

// Demo step 3a: two branches editing lines far apart merge on their own.
TEST(step3a_editsToDifferentLinesMergeWithoutHelp) {
    Project project;
    // Lines well apart, because neighbouring edits conflict the way Git's do.
    project.tree.write("notes.txt", "one\ntwo\nthree\nfour\nfive\nsix\nseven\n");
    project.repo.track("notes.txt");
    project.repo.commit("notes", kAuthor);

    project.repo.createBranch("experiment");
    project.tree.write("notes.txt", "ONE\ntwo\nthree\nfour\nfive\nsix\nseven\n");
    project.repo.commit("main changed the first line", kAuthor);

    project.repo.switchBranch("experiment");
    project.tree.write("notes.txt", "one\ntwo\nthree\nfour\nfive\nsix\nSEVEN\n");
    project.repo.commit("experiment changed the last line", "Tanishka");

    project.repo.switchBranch("main");
    const MergeOutcome outcome = project.repo.merge("experiment", kAuthor);
    CHECK(outcome.kind == MergeOutcome::Kind::Merged);
    CHECK(outcome.conflicts.empty());
    // Both edits survived in one file.
    CHECK(project.tree.read("notes.txt") == "ONE\ntwo\nthree\nfour\nfive\nsix\nSEVEN\n");
    CHECK(!project.repo.isMerging());
}

// Demo step 3b: the same line on both sides stops the merge, and committing is
// refused until the markers are gone.
TEST(step3b_theSameLineConflictsAndMustBeFixedByHand) {
    Project project;
    project.tree.write("notes.txt", "one\ntwo\nthree\n");
    project.repo.track("notes.txt");
    project.repo.commit("notes", kAuthor);

    project.repo.createBranch("experiment");
    project.tree.write("notes.txt", "one\nMAIN\nthree\n");
    project.repo.commit("main changed line 2", kAuthor);

    project.repo.switchBranch("experiment");
    project.tree.write("notes.txt", "one\nEXPERIMENT\nthree\n");
    project.repo.commit("experiment changed line 2", "Tanishka");

    project.repo.switchBranch("main");
    const MergeOutcome outcome = project.repo.merge("experiment", kAuthor);
    CHECK(outcome.kind == MergeOutcome::Kind::Conflicts);
    CHECK(outcome.conflicts.size() == 1);
    CHECK(project.repo.isMerging());

    // The explanation shown on stage is plain English (#19).
    const Conflict& conflict = outcome.conflicts.front().conflict;
    CHECK(conflict.location == "line 2");
    CHECK(!conflict.explanation.empty());
    CHECK(conflict.ours.find("MAIN") != std::string::npos);
    CHECK(conflict.theirs.find("EXPERIMENT") != std::string::npos);

    // The markers really are in the file, and committing with them is refused.
    CHECK(project.tree.read("notes.txt").find("<<<<<<<") != std::string::npos);
    CHECK_THROWS(project.repo.commit("half-fixed", kAuthor), MergeConflictException);

    // Fix it the way a person would, then commit.
    project.tree.write("notes.txt", "one\nMAIN and EXPERIMENT\nthree\n");
    project.repo.commit("kept both", kAuthor);
    CHECK(!project.repo.isMerging());
    CHECK(project.repo.getCommit(project.repo.getHead()).isMerge());
}

// Demo step 4: tamper with a saved file, and verify names it and its commit.
TEST(step4_verifyCatchesATamperedObjectAndNamesIt) {
    TempDir dir;
    {
        Repository repo = Repository::init(dir.path());
        WorkingTree tree(dir.path());
        tree.write("notes.txt", "the original words\n");
        repo.track("notes.txt");
        repo.commit("first", kAuthor);
    }

    // Clean to begin with.
    const IntegrityReport before = StorageManager(dir.path()).verify();
    CHECK(before.problems.empty());
    CHECK(before.commits == 1);
    CHECK(before.objects == 1);

    // Edit history behind Chrono's back, as the demo does in a text editor.
    const fs::path objects = dir.path() / ".chrono" / "objects";
    int tampered = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(objects)) {
        if (entry.is_regular_file()) {
            WorkingTree(objects).write(entry.path().filename().string(), "something else entirely\n");
            ++tampered;
        }
    }
    CHECK(tampered == 1);

    const IntegrityReport after = StorageManager(dir.path()).verify();
    CHECK(after.problems.size() == 1);
    // It has to say which file and which commit, or it isn't much of a demo.
    CHECK(after.problems.front().problem.find("notes.txt") != std::string::npos);
    CHECK(after.problems.front().problem.find("#1") != std::string::npos);
}

// Demo step 5: the finale. Two branches change different cells of the same
// row, which is one line of text, so a line-by-line merge can only call it a
// conflict. Chrono merges it, and the merged formula then reads the other
// branch's number: the semantic change the demo is about.
// The sheet keeps the row's name in column A, as a real budget would, which is
// also what lets the two sides' rows be paired up. See the known limit noted
// under step 5 below.
TEST(step5_differentCellsOfOneRowMergeAndTheFormulaSeesTheNewValue) {
    Project project;
    // Rent: B1 = 10, C1 = =B1*2
    const std::string base = sheet({{"Rent", "10", "=B1*2"}});
    project.tree.write("budget.csv", base);
    project.repo.track("budget.csv");
    project.repo.commit("base", kAuthor);
    CHECK(valueOf(base, "C1") == "20");

    project.repo.createBranch("experiment");

    // Branch X (main) changes the number to 50 and leaves the formula alone.
    const std::string ours = sheet({{"Rent", "50", "=B1*2"}});
    project.tree.write("budget.csv", ours);
    project.repo.commit("rent is now 50", kAuthor);
    CHECK(valueOf(ours, "C1") == "100");

    // Branch Y changes the formula and leaves the number alone.
    project.repo.switchBranch("experiment");
    const std::string theirs = sheet({{"Rent", "10", "=IF(B1>20,\"OVER BUDGET\",B1*2)"}});
    project.tree.write("budget.csv", theirs);
    project.repo.commit("C1 warns when B1 is over 20", "Tanishka");
    CHECK(valueOf(theirs, "C1") == "20");

    // One row, changed on both sides. To a line-by-line merge that is the same
    // line twice over, so Git can only call it a conflict; cell by cell there
    // is no clash at all.
    project.repo.switchBranch("main");
    const MergeOutcome outcome = project.repo.merge("experiment", kAuthor);
    // No cell was edited twice, but C1 now shows a value neither side saw (#34).
    CHECK(outcome.kind == MergeOutcome::Kind::Conflicts);
    CHECK(outcome.conflicts.size() == 1);
    CHECK(outcome.conflicts[0].conflict.location == "C1");
    CHECK(outcome.conflicts[0].conflict.semantic);
    CHECK(outcome.conflicts[0].conflict.explanation.find("reads B1") != std::string::npos);
    CHECK(outcome.conflicts[0].conflict.ours == "100");
    CHECK(outcome.conflicts[0].conflict.theirs == "20");

    const std::string merged = project.tree.read("budget.csv");
    const CSVHandler::Rows rows = CSVHandler::parseRows(merged);
    CHECK(rows.size() == 1);
    CHECK(rows.front().size() == 3);
    CHECK(rows.front()[1] == "50");                               // from main
    CHECK(rows.front()[2] == "=IF(B1>20,\"OVER BUDGET\",B1*2)");  // from experiment

    // The point of the whole project: neither person wrote this value. Main saw
    // 100, experiment saw 20, and the merge produces something else again. That
    // this is worth warning about is #34.
    CHECK(valueOf(merged, "C1") == "OVER BUDGET");

    // There are no markers to remove: once someone has looked, it commits.
    CHECK(project.repo.commit("checked the merged budget", kAuthor).isMerge());
}

// Demo step 6: the same work, asked for in plain English.
TEST(step6_theDemoCanBeDrivenByPlainEnglish) {
    Project project;
    threeCommits(project);

    const RuleBasedParser parser;
    const RequestPlanner planner(parser);

    // Read-only requests just run.
    const RequestPlan history = planner.plan("show me the history", contextFor(project.repo));
    CHECK(history.outcome == RequestPlan::Outcome::Run);
    CHECK(history.args == std::vector<std::string>{"log"});

    // Going back in time is understood, and asked about first (#18).
    const RequestPlan back = planner.plan("roll it back to the first commit", contextFor(project.repo));
    CHECK(back.outcome == RequestPlan::Outcome::Confirm);
    // Extra brackets: the comma inside the braces would otherwise look like a
    // second argument to the CHECK macro.
    CHECK((back.args == std::vector<std::string>{"revert", "1"}));
    CHECK(!back.question.empty());

    // A plan is nothing but command-line words, so the factory builds the same
    // command a typed one would, and it really does revert the files.
    project.run(back.args);
    CHECK(project.tree.read("budget.csv") == sheet({{"Item", "Q1"}, {"Rent", "1200"}}));

    // And it can be taken back, like anything else.
    const RequestPlan undo = planner.plan("undo that", contextFor(project.repo));
    CHECK(undo.outcome == RequestPlan::Outcome::Confirm);
    CHECK(undo.args == std::vector<std::string>{"undo"});
    project.run(undo.args);
    CHECK(project.tree.read("budget.csv") == sheet({{"Item", "Q1"}, {"Rent", "1300"}}));
}

// A time phrase has to land on a real commit, which is what makes the spoken
// version of step 2 possible at all (#17).
TEST(step6_aTimePhraseLandsOnARealCommit) {
    Project project;
    threeCommits(project);

    const RuleBasedParser parser;
    const RequestPlanner planner(parser);
    const RequestPlan plan = planner.plan("go back to how things were today", contextFor(project.repo));

    CHECK(plan.outcome == RequestPlan::Outcome::Confirm);
    CHECK(plan.args.size() == 2);
    CHECK(plan.args.front() == "revert");
    // Every commit was made moments ago, so "today" is the newest of them.
    CHECK(plan.args.back() == "3");
    // And it says so before anything is touched.
    CHECK(!plan.notes.empty());
}

// The whole script in one run, in order, on one repository: the rehearsal.
TEST(theWholeDemoRunsStartToFinish) {
    Project project;
    threeCommits(project);

    // 1. history and a cell-level diff
    CHECK(project.repo.log().size() == 3);
    CHECK(!project.repo.diff(1, 3).empty());

    // 2. back in time, then take it back
    project.run({"revert", "1"});
    CHECK(project.tree.read("budget.csv").find("1200") != std::string::npos);
    project.run({"undo"});
    CHECK(project.tree.read("budget.csv").find("1300") != std::string::npos);

    // 3. a branch, a clean merge of a far-apart change
    project.tree.write("notes.txt", "one\ntwo\nthree\nfour\nfive\n");
    project.repo.track("notes.txt");
    project.repo.commit("notes", kAuthor);
    project.repo.createBranch("experiment");
    project.repo.switchBranch("experiment");
    project.tree.write("notes.txt", "one\ntwo\nthree\nfour\nFIVE\n");
    project.repo.commit("experiment changed the last line", "Tanishka");
    project.repo.switchBranch("main");
    project.tree.write("notes.txt", "ONE\ntwo\nthree\nfour\nfive\n");
    project.repo.commit("main changed the first line", kAuthor);
    CHECK(project.repo.merge("experiment", kAuthor).kind == MergeOutcome::Kind::Merged);
    CHECK(project.tree.read("notes.txt") == "ONE\ntwo\nthree\nfour\nFIVE\n");

    // 4. nothing is damaged
    CHECK(StorageManager(project.dir.path()).verify().problems.empty());

    // 6. and it all still answers to plain English
    const RuleBasedParser parser;
    const RequestPlanner planner(parser);
    CHECK(planner.plan("where am I", contextFor(project.repo)).outcome == RequestPlan::Outcome::Run);
    CHECK(planner.plan("which branches are there", contextFor(project.repo)).args ==
          std::vector<std::string>{"branch"});
}
