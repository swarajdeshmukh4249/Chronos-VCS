#include "core/Repository.h"

#include <ctime>

#include "common/Exceptions.h"

namespace fs = std::filesystem;

Repository::Repository(const fs::path& root)
    : tree_(root), storage_(tree_.getRoot()), commits_(storage_.loadCommits()), tracked_(storage_.loadTracked()) {}

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
    Commit::Files files = currentFiles();
    if (commits_.empty() ? files.empty() : commits_.latest().hasSameFiles(files)) {
        throw NothingToCommitException("Nothing changed since the last commit");
    }

    const int id = static_cast<int>(commits_.size()) + 1;
    const int parentId = commits_.empty() ? 0 : commits_.latest().getId();
    Commit commit(id, parentId, author, message, std::time(nullptr), std::move(files));
    // Save first, so a failed write never leaves memory ahead of disk.
    storage_.saveCommit(commit);
    commits_.append(std::move(commit));
    return commits_.latest();
}

const Commit& Repository::getCommit(int id) const {
    if (id < 1 || !commits_.contains(static_cast<std::size_t>(id))) {
        throw CommitNotFoundException("There is no commit #" + std::to_string(id));
    }
    return commits_.at(static_cast<std::size_t>(id));
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
