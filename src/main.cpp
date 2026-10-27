#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "common/Exceptions.h"
#include "core/Repository.h"

namespace fs = std::filesystem;

namespace {

// Phase 1 command line. The Command pattern (#14) and plain-English input (#16) replace this later.
const char* const kUsage =
    "usage: chrono <command>\n"
    "  init                  start tracking history in this folder\n"
    "  add <file>...         include files in future commits\n"
    "  commit -m <message>   save a new version of every tracked file\n"
    "  log                   list commits, newest first\n"
    "  show <id>             show one commit's details and files\n"
    "  status                show whether there are uncommitted changes\n"
    "  verify                check that no saved history was changed or lost\n"
    "  checkout <id>         put the files from a commit back\n"
    "  revert <id>           put a commit's files back and record it as a new commit\n";

std::string author() {
    for (const char* name : {"CHRONO_AUTHOR", "USER", "USERNAME"}) {
        if (const char* value = std::getenv(name); value && *value) {
            return value;
        }
    }
    return "unknown";
}

int parseId(const std::string& text) {
    std::size_t used = 0;
    int id = 0;
    try {
        id = std::stoi(text, &used);
    } catch (const std::exception&) {
        used = 0;
    }
    if (used == 0 || used != text.size()) {
        throw CommitNotFoundException("\"" + text + "\" is not a commit number");
    }
    return id;
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

    if (command == "add" && args.size() >= 2) {
        for (std::size_t i = 1; i < args.size(); ++i) {
            std::cout << "Tracking " << repo.track(fs::absolute(args[i])) << "\n";
        }
    } else if (command == "commit" && args.size() == 3 && args[1] == "-m") {
        const Commit& commit = repo.commit(args[2], author());
        const std::size_t count = commit.getFiles().size();
        std::cout << "Saved commit #" << commit.getId() << " (" << count << (count == 1 ? " file)\n" : " files)\n");
    } else if (command == "log" && args.size() == 1) {
        if (repo.log().empty()) {
            std::cout << "No commits yet\n";
        }
        for (std::size_t id = repo.log().size(); id >= 1; --id) {
            const Commit& commit = repo.log().at(id);
            std::cout << (commit.getId() == repo.getHead() ? "* " : "  ") << commit.summary() << "\n";
        }
    } else if (command == "show" && args.size() == 2) {
        const Commit& commit = repo.getCommit(parseId(args[1]));
        std::cout << "Commit #" << commit.getId() << (commit.getId() == repo.getHead() ? " (current)" : "") << "\n"
                  << "Parent:  " << (commit.hasParent() ? "#" + std::to_string(commit.getParentId()) : "none") << "\n"
                  << "Author:  " << commit.getAuthor() << "\n"
                  << "Date:    " << commit.formattedTime() << "\n"
                  << "Message: " << commit.getMessage() << "\n\n"
                  << "Files (" << commit.getFiles().size() << "):\n";
        for (const auto& [path, snapshot] : commit.getFiles()) {
            const std::size_t bytes = snapshot.getContent().size();
            std::cout << "  " << path << "  (" << bytes << (bytes == 1 ? " byte)\n" : " bytes)\n");
        }
    } else if (command == "status" && args.size() == 1) {
        const std::size_t count = repo.getTracked().size();
        std::cout << "On commit #" << repo.getHead() << ", tracking " << count << (count == 1 ? " file\n" : " files\n")
                  << (repo.hasUncommittedChanges() ? "You have uncommitted changes\n" : "Nothing to commit\n");
    } else if (command == "checkout" && args.size() == 2) {
        const int id = parseId(args[1]);
        repo.checkout(id);
        std::cout << "Files are now as they were in commit #" << id << "\n";
    } else if (command == "revert" && args.size() == 2) {
        const Commit& commit = repo.revert(parseId(args[1]), author());
        std::cout << "Saved commit #" << commit.getId() << ": " << commit.getMessage() << "\n";
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
