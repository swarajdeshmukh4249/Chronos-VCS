#include "core/Command.h"

#include <algorithm>
#include <exception>
#include <filesystem>

#include "common/Exceptions.h"

int Command::run(Repository& repo, std::ostream& out) {
    if (!canBeUndone()) {
        return execute(repo, out);
    }
    const RepositoryState before = repo.captureState(describe());
    const int code = execute(repo, out);
    repo.rememberForUndo(before);
    return code;
}

std::string AddCommand::describe() const {
    std::string files;
    for (const std::string& path : paths_) {
        files += (files.empty() ? "" : ", ") + path;
    }
    return "add " + files;
}

int AddCommand::execute(Repository& repo, std::ostream& out) {
    for (const std::string& path : paths_) {
        out << "Tracking " << repo.track(std::filesystem::absolute(path)) << "\n";
    }
    return 0;
}

std::string CommitCommand::describe() const {
    return "commit \"" + message_ + "\"";
}

int CommitCommand::execute(Repository& repo, std::ostream& out) {
    const bool finishesMerge = repo.isMerging();
    const Commit& commit = repo.commit(message_, author_);
    const std::size_t count = commit.getFiles().size();
    out << "Saved " << (finishesMerge ? "merge commit #" : "commit #") << commit.getId() << " (" << count
        << (count == 1 ? " file)\n" : " files)\n");
    return 0;
}

std::string CheckoutCommand::describe() const {
    return "checkout of commit #" + std::to_string(commitId_);
}

int CheckoutCommand::execute(Repository& repo, std::ostream& out) {
    repo.checkout(commitId_);
    out << "Files are now as they were in commit #" << commitId_ << "\n";
    return 0;
}

std::string RevertCommand::describe() const {
    return "revert to commit #" + std::to_string(commitId_);
}

int RevertCommand::execute(Repository& repo, std::ostream& out) {
    const Commit& commit = repo.revert(commitId_, author_);
    out << "Saved commit #" << commit.getId() << ": " << commit.getMessage() << "\n";
    return 0;
}

std::string CreateBranchCommand::describe() const {
    return "new branch " + name_;
}

int CreateBranchCommand::execute(Repository& repo, std::ostream& out) {
    const Branch& branch = repo.createBranch(name_);
    out << "Created branch " << branch.getName() << " at commit #" << branch.getCommitId() << ". Use \"chrono switch "
        << branch.getName() << "\" to work on it.\n";
    return 0;
}

std::string SwitchCommand::describe() const {
    return "switch to branch " + name_;
}

int SwitchCommand::execute(Repository& repo, std::ostream& out) {
    repo.switchBranch(name_);
    out << "Switched to branch " << repo.getCurrentBranch() << "\n";
    return 0;
}

std::string MergeCommand::describe() const {
    return "merge of branch " + branch_;
}

int MergeCommand::execute(Repository& repo, std::ostream& out) {
    const MergeOutcome outcome = repo.merge(branch_, author_);
    switch (outcome.kind) {
    case MergeOutcome::Kind::AlreadyUpToDate:
        out << "Already up to date: everything on " << branch_ << " is already here.\n";
        return 0;
    case MergeOutcome::Kind::FastForward:
        out << "Fast-forwarded to commit #" << outcome.commitId << ". This branch was simply behind " << branch_
            << ", so no merge commit was needed.\n";
        return 0;
    case MergeOutcome::Kind::Merged:
        out << "Merged " << branch_ << " and saved merge commit #" << outcome.commitId << ".\n";
        return 0;
    case MergeOutcome::Kind::Conflicts:
        break;
    }
    out << "Merge stopped: " << outcome.conflicts.size()
        << (outcome.conflicts.size() == 1 ? " conflict" : " conflicts") << " to fix.\n";
    for (const MergeOutcome::FileConflict& file : outcome.conflicts) {
        out << "  " << file.path << ", " << file.conflict.location << ": " << file.conflict.explanation << "\n";
    }
    const bool sheet = std::any_of(outcome.conflicts.begin(), outcome.conflicts.end(),
                                   [](const MergeOutcome::FileConflict& file) { return Repository::isSheet(file.path); });
    out << "\nIn each file, between <<<<<<< and >>>>>>>, \"ours\" is this branch and \"theirs\" is " << branch_
        << ".\n"
        << (sheet ? "In a .csv file both values sit inside the cell: replace the whole cell with the one you want.\n"
                  : "")
        << "Keep the lines you want, delete the markers, then run chrono commit -m \"<message>\".\n"
        << "To cancel instead, run chrono merge --abort (or chrono undo).\n";
    return 1;
}

std::string AbortMergeCommand::describe() const {
    return "cancelled merge";
}

int AbortMergeCommand::execute(Repository& repo, std::ostream& out) {
    repo.abortMerge();
    out << "Merge cancelled. Your files are back as they were.\n";
    return 0;
}

std::string UndoCommand::describe() const {
    return "undo";
}

int UndoCommand::execute(Repository& repo, std::ostream& out) {
    const bool mergeWasWaiting = repo.isMerging();
    const std::string undone = repo.undo();
    out << "Undid the " << undone << ". Files and branches are back as they were before it.\n";
    if (mergeWasWaiting && undone.rfind("merge", 0) == 0) {
        out << "The merge had stopped at conflicts, so no commit was made.\n";
    } else if (undone.rfind("commit", 0) == 0 || undone.rfind("revert", 0) == 0 || undone.rfind("merge", 0) == 0) {
        out << "The commit itself is still saved; see chrono log --all.\n";
    }
    return 0;
}

std::unique_ptr<Command> CommandFactory::create(const std::vector<std::string>& args, const std::string& author) {
    if (args.empty()) {
        return nullptr;
    }
    const std::string& name = args[0];
    const std::size_t count = args.size();
    if (name == "add" && count >= 2) {
        return std::make_unique<AddCommand>(std::vector<std::string>(args.begin() + 1, args.end()));
    }
    if (name == "commit" && count == 3 && args[1] == "-m") {
        return std::make_unique<CommitCommand>(args[2], author);
    }
    if (name == "checkout" && count == 2) {
        return std::make_unique<CheckoutCommand>(parseCommitId(args[1]));
    }
    if (name == "revert" && count == 2) {
        return std::make_unique<RevertCommand>(parseCommitId(args[1]), author);
    }
    if (name == "branch" && count == 2) {
        return std::make_unique<CreateBranchCommand>(args[1]);
    }
    if (name == "switch" && count == 2) {
        return std::make_unique<SwitchCommand>(args[1]);
    }
    if (name == "merge" && count == 2) {
        if (args[1] == "--abort") {
            return std::make_unique<AbortMergeCommand>();
        }
        return std::make_unique<MergeCommand>(args[1], author);
    }
    if (name == "undo" && count == 1) {
        return std::make_unique<UndoCommand>();
    }
    return nullptr;
}

int CommandFactory::parseCommitId(const std::string& text) {
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
