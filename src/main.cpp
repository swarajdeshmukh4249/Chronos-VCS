#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// MinGW's headers already define NOMINMAX.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#endif

#include "common/Exceptions.h"
#include "core/Command.h"
#include "core/HtmlView.h"
#include "core/Repository.h"
#include "nlp/RequestPlanner.h"
#include "nlp/RuleBasedParser.h"

namespace fs = std::filesystem;

namespace {

// Plain-English requests come back out as ordinary command-line words and run
// through the same dispatch as anything typed. Declared here because `say` is
// handled inside run() itself.
int run(const std::vector<std::string>& args);

// Commands that change the repository come from CommandFactory; read-only ones
// (log, show, diff, status, verify, listing branches) are handled here.
const char* const kUsage =
    "usage: chrono <command>\n"
    "  init                  start tracking history in this folder\n"
    "  add <file>...         include files in future commits\n"
    "  commit -m <message>   save a new version of every tracked file\n"
    "  log [--all]           list this branch's commits, newest first (--all: every commit)\n"
    "  branch                list branches\n"
    "  branch <name>         start a new branch at the current commit\n"
    "  switch <name>         work on another branch\n"
    "  merge <branch>        bring another branch's changes into this one\n"
    "  merge --abort         cancel a merge that stopped at conflicts\n"
    "  show <id>             show one commit's details and files\n"
    "  diff [<from> <to>]    show changed lines since the last commit, or between two commits\n"
    "  status                show whether there are uncommitted changes\n"
    "  verify                check that no saved history was changed or lost\n"
    "  checkout <id>         put the files from a commit back\n"
    "  revert <id>           put a commit's files back and record it as a new commit\n"
    "  undo                  take back the last command that changed something\n"
    "  view [--no-open]      draw the history, branches and sheets as a web page and open it\n"
    "  say <request>         do what a plain-English request asks, e.g.\n"
    "                        chrono say \"go back to how things were yesterday evening\"\n"
    "                        add -y to skip the question before anything is overwritten\n";

std::string author() {
    for (const char* name : {"CHRONO_AUTHOR", "USER", "USERNAME"}) {
        if (const char* value = std::getenv(name); value && *value) {
            return value;
        }
    }
    return "unknown";
}

// Colour only when writing to a terminal, and never when NO_COLOR is set (https://no-color.org).
bool useColour() {
    if (std::getenv("NO_COLOR")) {
        return false;
    }
#ifdef _WIN32
    if (!_isatty(_fileno(stdout))) {
        return false;
    }
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    return GetConsoleMode(out, &mode) && SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

// Prints each line of block with a prefix, e.g. "- old line" in red.
void printLines(const std::string& block, const char* prefix, const char* colour, bool colours) {
    std::istringstream lines(block);
    for (std::string line; std::getline(lines, line);) {
        std::cout << (colours ? colour : "") << prefix << line << (colours ? "\033[0m" : "") << "\n";
    }
}

void printDiffs(const std::vector<FileDiff>& diffs) {
    if (diffs.empty()) {
        std::cout << "No differences\n";
        return;
    }
    const bool colours = useColour();
    for (const FileDiff& file : diffs) {
        const char* status = file.status == FileDiff::Status::Added     ? " (new file)"
                             : file.status == FileDiff::Status::Removed ? " (removed)"
                                                                        : "";
        std::cout << (colours ? "\033[1m" : "") << file.path << status << (colours ? "\033[0m" : "") << "\n";
        for (const Change& change : file.result.changes) {
            std::cout << (colours ? "\033[36m" : "") << "@ " << change.location << (colours ? "\033[0m" : "") << "\n";
            printLines(change.before, "- ", "\033[31m", colours);
            printLines(change.after, "+ ", "\033[32m", colours);
        }
        std::cout << "\n";
    }
}

// The picture of the repository that the plain-English layer reads. It takes
// this rather than the Repository itself, so src/nlp doesn't depend on src/core.
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

// Puts a word back in quotes if it needs them, so the line printed as "this
// runs" can be copied and pasted.
std::string shellWord(const std::string& word) {
    return word.find(' ') == std::string::npos ? word : "\"" + word + "\"";
}

// Anything but a clear yes counts as no, so a stray keypress, or input that
// isn't a terminal at all, never starts a command that overwrites files.
bool confirmed(const std::string& question) {
    std::cout << question << " [y/N] " << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer)) {
        std::cout << "\n";
        return false;
    }
    std::string trimmed;
    for (char c : answer) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            trimmed += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return trimmed == "y" || trimmed == "yes";
}

int say(const Repository& repo, const std::vector<std::string>& words, bool assumeYes) {
    std::string request;
    for (const std::string& word : words) {
        request += (request.empty() ? "" : " ") + word;
    }

    const RuleBasedParser parser;
    const RequestPlanner planner(parser);
    const RequestPlan plan = planner.plan(request, contextFor(repo));

    if (plan.outcome == RequestPlan::Outcome::Unclear) {
        for (const std::string& problem : plan.problems) {
            std::cout << problem << "\n";
        }
        return 1;
    }
    if (plan.outcome == RequestPlan::Outcome::DidYouMean) {
        std::cout << (plan.options.size() == 1 ? "Did you mean this?\n" : "That could mean more than one thing:\n");
        for (const std::string& option : plan.options) {
            std::cout << "  - " << option << "\n";
        }
        std::cout << "Say which one you want, or type the command itself.\n";
        return 1;
    }

    std::cout << "Understood: " << plan.action << "\n";
    for (const std::string& note : plan.notes) {
        std::cout << note << "\n";
    }
    std::string line = "chrono";
    for (const std::string& word : plan.args) {
        line += " " + shellWord(word);
    }
    std::cout << "Running: " << line << "\n";

    if (plan.outcome == RequestPlan::Outcome::Confirm && !assumeYes && !confirmed(plan.question)) {
        std::cout << "Nothing was changed.\n";
        return 1;
    }
    std::cout << "\n";
    return run(plan.args);
}

int run(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << kUsage;
        return 1;
    }
    const std::string& command = args[0];

    if (command == "init") {
        const Repository repo = Repository::init(fs::current_path());
        std::cout << "Started a Chrono repository in " << repo.getRoot().string() << "\n";
        return 0;
    }

    // Runs before opening the repository, because opening stops at the first damaged commit.
    if (command == "verify" && args.size() == 1) {
        const IntegrityReport report = StorageManager(StorageManager::findRoot(fs::current_path())).verify();
        if (report.problems.empty()) {
            std::cout << "All " << report.commits << " commits and " << report.objects
                      << " saved file versions are intact\n";
            return 0;
        }
        std::cout << "Found " << report.problems.size() << (report.problems.size() == 1 ? " problem:\n" : " problems:\n");
        for (const IntegrityProblem& problem : report.problems) {
            std::cout << "  " << problem.file << " " << problem.problem << "\n";
        }
        return 1;
    }

    Repository repo = Repository::open(fs::current_path());

    // Plain English: work out which command is meant, ask first if it could
    // overwrite anything, then run it like any other command.
    if (command == "say" || command == "ask") {
        bool assumeYes = false;
        std::vector<std::string> words;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-y" || args[i] == "--yes") {
                assumeYes = true;
            } else {
                words.push_back(args[i]);
            }
        }
        if (words.empty()) {
            std::cout << "usage: chrono say \"<what you want, in plain English>\"\n"
                      << "  e.g. chrono say \"go back to how things were yesterday evening\"\n";
            return 1;
        }
        return say(repo, words, assumeYes);
    }

    // Commands that change the repository are Command objects, so undo can take them back.
    if (std::unique_ptr<Command> change = CommandFactory::create(args, author())) {
        return change->run(repo, std::cout);
    }

    if (command == "view" && (args.size() == 1 || (args.size() == 2 && args[1] == "--no-open"))) {
        const fs::path page = repo.getRoot() / ".chrono" / "view.html";
        std::ofstream(page, std::ios::binary) << HtmlView::render(repo);
        std::cout << "Drew the repository in " << page.string() << "\n";
        if (args.size() == 1) {
#ifdef _WIN32
            const std::string open = "start \"\" \"" + page.string() + "\"";
#elif defined(__APPLE__)
            const std::string open = "open \"" + page.string() + "\"";
#else
            const std::string open = "xdg-open \"" + page.string() + "\" >/dev/null 2>&1 &";
#endif
            if (std::system(open.c_str()) != 0) {
                std::cout << "Open that file in a web browser to see it.\n";
            }
        }
        return 0;
    }

    if (command == "log" && (args.size() == 1 || (args.size() == 2 && args[1] == "--all"))) {
        std::vector<int> ids = repo.historyOfHead();
        if (args.size() == 2) {
            ids.clear();
            for (std::size_t id = repo.log().size(); id >= 1; --id) {
                ids.push_back(static_cast<int>(id));
            }
        }
        if (ids.empty()) {
            std::cout << "No commits yet\n";
        }
        const std::vector<Branch> branches = repo.getBranches();
        for (int id : ids) {
            std::string labels;
            for (const Branch& branch : branches) {
                if (branch.getCommitId() == id) {
                    labels += (labels.empty() ? "" : ", ") + branch.getName();
                }
            }
            const Commit& commit = repo.getCommit(id);
            std::cout << (id == repo.getHead() ? "* " : "  ") << commit.summary()
                      << (commit.isMerge() ? "  [merged #" + std::to_string(commit.getMergedParentId()) + "]" : "")
                      << (labels.empty() ? "" : "  (" + labels + ")") << "\n";
        }
    } else if (command == "branch" && args.size() == 1) {
        for (const Branch& branch : repo.getBranches()) {
            const bool current = branch.getName() == repo.getCurrentBranch();
            std::cout << (current ? "* " : "  ") << branch.getName() << "  "
                      << (branch.getCommitId() == 0 ? "(no commits yet)" : "#" + std::to_string(branch.getCommitId()))
                      << "\n";
        }
    } else if (command == "show" && args.size() == 2) {
        const Commit& commit = repo.getCommit(CommandFactory::parseCommitId(args[1]));
        std::cout << "Commit #" << commit.getId() << (commit.getId() == repo.getHead() ? " (current)" : "") << "\n"
                  << "Parent:  " << (commit.hasParent() ? "#" + std::to_string(commit.getParentId()) : "none")
                  << (commit.isMerge() ? " and #" + std::to_string(commit.getMergedParentId()) + " (merge)" : "")
                  << "\n"
                  << "Author:  " << commit.getAuthor() << "\n"
                  << "Date:    " << commit.formattedTime() << "\n"
                  << "Message: " << commit.getMessage() << "\n\n"
                  << "Files (" << commit.getFiles().size() << "):\n";
        for (const auto& [path, snapshot] : commit.getFiles()) {
            const std::size_t bytes = snapshot.getContent().size();
            std::cout << "  " << path << "  (" << bytes << (bytes == 1 ? " byte)\n" : " bytes)\n");
        }
    } else if (command == "diff" && args.size() == 1) {
        printDiffs(repo.diffUncommitted());
    } else if (command == "diff" && args.size() == 3) {
        printDiffs(repo.diff(CommandFactory::parseCommitId(args[1]), CommandFactory::parseCommitId(args[2])));
    } else if (command == "status" && args.size() == 1) {
        const std::size_t count = repo.getTracked().size();
        if (repo.getCurrentBranch().empty()) {
            std::cout << "Not on a branch (at commit #" << repo.getHead() << "). New commits won't move any branch.\n";
        } else {
            std::cout << "On branch " << repo.getCurrentBranch() << " (commit #" << repo.getHead() << ")\n";
        }
        if (repo.isMerging()) {
            std::cout << "Merging " << repo.getPendingMerge().branch
                      << ": fix the conflicts and commit, or run chrono merge --abort\n";
        }
        std::cout << "Tracking " << count << (count == 1 ? " file\n" : " files\n")
                  << (repo.hasUncommittedChanges() ? "You have uncommitted changes\n" : "Nothing to commit\n");
        const std::string undoable = repo.nextUndo();
        if (!undoable.empty()) {
            std::cout << "chrono undo would take back the " << undoable << "\n";
        }
    } else {
        std::cout << kUsage;
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(std::vector<std::string>(argv + 1, argv + argc));
    } catch (const ChronoException& e) {
        std::cerr << "error: " << e.what() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "unexpected error: " << e.what() << "\n";
    }
    return 1;
}
