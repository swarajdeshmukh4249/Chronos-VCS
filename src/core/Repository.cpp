#include "core/Repository.h"

#include <ctime>

#include "common/Exceptions.h"

namespace fs = std::filesystem;

Repository::Repository(const fs::path& root)
    : tree_(root),
      storage_(tree_.getRoot()),
      commits_(storage_.loadCommits()),
      tracked_(storage_.loadTracked()),
      head_(storage_.loadHead()) {
    if (head_ != 0 && !commits_.contains(static_cast<std::size_t>(head_))) {
        throw CorruptObjectException("HEAD points to commit #" + std::to_string(head_) + ", which doesn't exist");
    }
}

Repository Repository::init(const fs::path& root) {
    StorageManager(root).initialize();
    return Repository(root);
}

Repository Repository::open(const fs::path& start) {
    return Repository(StorageManager::findRoot(start));
}

std::string Repository::track(const fs::path& path) {
    const std::string relative = tree_.relativePath(path);
    if (!tree_.exists(relative)) {
        throw FileNotFoundException("No file named \"" + relative + "\"");
    }
    if (tracked_.insert(relative).second) {
        storage_.saveTracked(tracked_);
    }
    return relative;
}

const Commit& Repository::commit(const std::string& message, const std::string& author) {
    if (message.empty()) {
        throw ChronoException("A commit needs a message");
    }
    if (tracked_.empty()) {
        throw NothingToCommitException("Nothing is tracked yet. Add a file first.");
    }
    if (!hasUncommittedChanges()) {
        throw NothingToCommitException("Nothing changed since the last commit");
    }

    const int id = static_cast<int>(commits_.size()) + 1;
    Commit commit(id, head_, author, message, std::time(nullptr), currentFiles());
    // Save first, so a failed write never leaves memory ahead of disk.
    storage_.saveCommit(commit);
    storage_.saveHead(id);
    commits_.append(std::move(commit));
    head_ = id;
    return commits_.latest();
}

const Commit& Repository::getCommit(int id) const {
    if (id < 1 || !commits_.contains(static_cast<std::size_t>(id))) {
        throw CommitNotFoundException("There is no commit #" + std::to_string(id));
    }
    return commits_.at(static_cast<std::size_t>(id));
}

bool Repository::hasUncommittedChanges() const {
    const Commit::Files files = currentFiles();
    return head_ == 0 ? !files.empty() : !getCommit(head_).hasSameFiles(files);
}

void Repository::checkout(int id) {
    const Commit& target = getCommit(id);
    requireNoUncommittedChanges();
    restoreFiles(target);
    storage_.saveHead(id);
    head_ = id;
}

const Commit& Repository::revert(int id, const std::string& author) {
    const Commit& target = getCommit(id);
    requireNoUncommittedChanges();
    // Built before commit() runs, because appending a commit can move target.
    const std::string message = "Revert to #" + std::to_string(id) + ": " + target.getMessage();
    restoreFiles(target);
    return commit(message, author);
}

void Repository::requireNoUncommittedChanges() const {
    if (hasUncommittedChanges()) {
        throw UncommittedChangesException("You have changes that aren't committed yet. Commit them first.");
    }
}

void Repository::restoreFiles(const Commit& target) {
    for (const std::string& path : tracked_) {
        if (!target.findFile(path)) {
            tree_.remove(path);
        }
    }
    for (const auto& [path, snapshot] : target.getFiles()) {
        tree_.write(path, snapshot.getContent());
        tracked_.insert(path);
    }
    storage_.saveTracked(tracked_);
}

Commit::Files Repository::currentFiles() const {
    Commit::Files files;
    for (const std::string& path : tracked_) {
        if (tree_.exists(path)) {
            files.emplace(path, Snapshot(path, tree_.read(path)));
        }
    }
    return files;
}
