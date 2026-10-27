#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
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
#include "core/Repository.h"

namespace fs = std::filesystem;

namespace {

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
    "  undo                  take back the last command that changed something\n";

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

    // Commands that change the repository are Command objects, so undo can take them back.
    if (std::unique_ptr<Command> change = CommandFactory::create(args, author())) {
        return change->run(repo, std::cout);
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
