#include "core/Repository.h"

#include <ctime>

#include "common/Exceptions.h"
#include "core/LCSDiff.h"

namespace fs = std::filesystem;

Repository::Repository(const fs::path& root)
    : tree_(root), storage_(tree_.getRoot()), commits_(storage_.loadCommits()), tracked_(storage_.loadTracked()) {
    auto exists = [this](int id) { return id == 0 || commits_.contains(static_cast<std::size_t>(id)); };
    for (const Branch& branch : storage_.loadBranches()) {
        if (!exists(branch.getCommitId())) {
            throw CorruptObjectException("Branch \"" + branch.getName() + "\" points to commit #" +
                                         std::to_string(branch.getCommitId()) + ", which doesn't exist");
        }
        branches_.emplace(branch.getName(), branch);
    }

    const HeadRef head = storage_.loadHead();
    if (head.branch.empty()) {
        head_ = head.commitId;
    } else {
        auto it = branches_.find(head.branch);
        if (it == branches_.end()) {
            throw CorruptObjectException("HEAD points to branch \"" + head.branch + "\", which doesn't exist");
        }
        branch_ = head.branch;
        head_ = it->second.getCommitId();
    }
    if (!exists(head_)) {
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
    Commit::Files files = currentFiles();
    if (!differsFromHead(files)) {
        throw NothingToCommitException("Nothing changed since the last commit");
    }

    const int id = static_cast<int>(commits_.size()) + 1;
    Commit commit(id, head_, author, message, std::time(nullptr), std::move(files));
    // Save first, so a failed write never leaves memory ahead of disk.
    storage_.saveCommit(commit);
    commits_.append(std::move(commit));
    moveHeadTo(id);
    return commits_.latest();
}

std::vector<int> Repository::historyOfHead() const {
    if (head_ == 0) {
        return {};
    }
    const std::set<int> ancestors = ancestorsOf(head_);
    return std::vector<int>(ancestors.rbegin(), ancestors.rend());
}

std::set<int> Repository::ancestorsOf(int id) const {
    std::set<int> seen;
    std::vector<int> stack{id};
    while (!stack.empty()) {
        const int current = stack.back();
        stack.pop_back();
        if (current == 0 || !seen.insert(current).second) {
            continue;
        }
        const Commit& commit = getCommit(current);
        stack.push_back(commit.getParentId());
        stack.push_back(commit.getMergedParentId());
    }
    return seen;
}

int Repository::mergeBase(int a, int b) const {
    const std::set<int> ofA = ancestorsOf(a);
    const std::set<int> ofB = ancestorsOf(b);
    for (auto it = ofA.rbegin(); it != ofA.rend(); ++it) {
        if (ofB.count(*it)) {
            return *it;
        }
    }
    return 0;
}

std::vector<Branch> Repository::getBranches() const {
    std::vector<Branch> branches;
    for (const auto& entry : branches_) {
        branches.push_back(entry.second);
    }
    return branches;
}

const Branch& Repository::createBranch(const std::string& name) {
    Branch branch(name, head_);
    for (const auto& entry : branches_) {
        if (Branch::sameNameIgnoringCase(entry.first, name)) {
            throw BranchExistsException("A branch named \"" + entry.first + "\" already exists");
        }
    }
    storage_.saveBranch(branch);
    return branches_.emplace(name, branch).first->second;
}

void Repository::switchBranch(const std::string& name) {
    auto it = branches_.find(name);
    if (it == branches_.end()) {
        throw BranchNotFoundException("There is no branch named \"" + name + "\"");
    }
    requireNoUncommittedChanges();
    const int id = it->second.getCommitId();
    restoreFiles(id == 0 ? Commit::Files{} : getCommit(id).getFiles());
    storage_.saveHead(HeadRef{name, 0});
    branch_ = name;
    head_ = id;
}

const Commit& Repository::getCommit(int id) const {
    if (id < 1 || !commits_.contains(static_cast<std::size_t>(id))) {
        throw CommitNotFoundException("There is no commit #" + std::to_string(id));
    }
    return commits_.at(static_cast<std::size_t>(id));
}

std::vector<FileDiff> Repository::diff(int fromId, int toId) const {
    return compare(getCommit(fromId).getFiles(), getCommit(toId).getFiles());
}

std::vector<FileDiff> Repository::diffUncommitted() const {
    return compare(head_ == 0 ? Commit::Files{} : getCommit(head_).getFiles(), currentFiles());
}

std::vector<FileDiff> Repository::compare(const Commit::Files& before, const Commit::Files& after) {
    std::set<std::string> paths;
    for (const auto& entry : before) {
        paths.insert(entry.first);
    }
    for (const auto& entry : after) {
        paths.insert(entry.first);
    }

    std::vector<FileDiff> diffs;
    for (const std::string& path : paths) {
        auto old = before.find(path);
        auto now = after.find(path);
        const Snapshot empty(path, "");
        const Snapshot& oldFile = old == before.end() ? empty : old->second;
        const Snapshot& newFile = now == after.end() ? empty : now->second;
        if (old != before.end() && now != after.end() && oldFile.getContent() == newFile.getContent()) {
            continue;
        }
        const FileDiff::Status status = old == before.end()  ? FileDiff::Status::Added
                                        : now == after.end() ? FileDiff::Status::Removed
                                                             : FileDiff::Status::Modified;
        diffs.push_back({path, status, diffAlgorithmFor(path)->diff(oldFile, newFile)});
    }
    return diffs;
}

std::unique_ptr<DiffAlgorithm> Repository::diffAlgorithmFor(const std::string&) {
    return std::make_unique<LCSDiff>();
}

bool Repository::hasUncommittedChanges() const {
    return differsFromHead(currentFiles());
}

bool Repository::differsFromHead(const Commit::Files& files) const {
    return head_ == 0 ? !files.empty() : !getCommit(head_).hasSameFiles(files);
}

void Repository::checkout(int id) {
    const Commit& target = getCommit(id);
    requireNoUncommittedChanges();
    restoreFiles(target.getFiles());
    storage_.saveHead(HeadRef{"", id});
    branch_.clear();
    head_ = id;
}

const Commit& Repository::revert(int id, const std::string& author) {
    const Commit& target = getCommit(id);
    requireNoUncommittedChanges();
    // Built before commit() runs, because appending a commit can move target.
    const std::string message = "Revert to #" + std::to_string(id) + ": " + target.getMessage();
    restoreFiles(target.getFiles());
    return commit(message, author);
}

void Repository::moveHeadTo(int id) {
    if (branch_.empty()) {
        storage_.saveHead(HeadRef{"", id});
    } else {
        Branch& branch = branches_.at(branch_);
        branch.moveTo(id);
        storage_.saveBranch(branch);
    }
    head_ = id;
}

void Repository::requireNoUncommittedChanges() const {
    if (hasUncommittedChanges()) {
        throw UncommittedChangesException("You have changes that aren't committed yet. Commit them first.");
    }
}

void Repository::restoreFiles(const Commit::Files& files) {
    for (const std::string& path : tracked_) {
        if (files.find(path) == files.end()) {
            tree_.remove(path);
        }
    }
    for (const auto& [path, snapshot] : files) {
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
