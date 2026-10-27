#include "core/Repository.h"

#include <ctime>
#include <sstream>

#include "common/Exceptions.h"
#include "core/LCSDiff.h"
#include "core/LineMerge.h"

namespace fs = std::filesystem;

namespace {

// True when any of the three lines LineMerge writes around a conflict is still in content.
bool hasConflictMarkers(const std::string& content) {
    std::istringstream lines(content);
    for (std::string line; std::getline(lines, line);) {
        if (line.rfind("<<<<<<< ", 0) == 0 || line == "=======" || line.rfind(">>>>>>> ", 0) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

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

    pending_ = storage_.loadPendingMerge();
    if (pending_.commitId != 0 && !exists(pending_.commitId)) {
        throw CorruptObjectException("A merge is waiting on commit #" + std::to_string(pending_.commitId) +
                                     ", which doesn't exist");
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
    if (isMerging()) {
        for (const auto& [path, snapshot] : files) {
            const std::string& content = snapshot.getContent();
            if (hasConflictMarkers(content)) {
                throw MergeConflictException(path + " still has conflict markers. Edit it to keep the lines you "
                                                    "want, then commit again.");
            }
        }
    } else if (!differsFromHead(files)) {
        // A merge is recorded even if the result matches HEAD, so only plain commits need a change.
        throw NothingToCommitException("Nothing changed since the last commit");
    }

    const Commit& saved = saveNewCommit(message, author, std::move(files), pending_.commitId);
    if (isMerging()) {
        pending_ = PendingMerge{};
        storage_.savePendingMerge(pending_);
    }
    return saved;
}

const Commit& Repository::saveNewCommit(const std::string& message, const std::string& author, Commit::Files files,
                                        int mergedParentId) {
    const int id = static_cast<int>(commits_.size()) + 1;
    Commit commit(id, head_, author, message, std::time(nullptr), std::move(files), mergedParentId);
    // Save first, so a failed write never leaves memory ahead of disk.
    storage_.saveCommit(commit);
    commits_.append(std::move(commit));
    moveHeadTo(id);
    return commits_.latest();
}

MergeOutcome Repository::merge(const std::string& name, const std::string& author) {
    requireNoPendingMerge();
    const Branch& found = findBranch(name);
    const std::string branch = found.getName();
    requireNoUncommittedChanges();

    MergeOutcome outcome;
    const int theirs = found.getCommitId();
    if (theirs == 0 || theirs == head_ || (head_ != 0 && ancestorsOf(head_).count(theirs))) {
        outcome.kind = MergeOutcome::Kind::AlreadyUpToDate;
        outcome.commitId = head_;
        return outcome;
    }
    if (head_ == 0 || ancestorsOf(theirs).count(head_)) {
        restoreFiles(getCommit(theirs).getFiles());
        moveHeadTo(theirs);
        outcome.kind = MergeOutcome::Kind::FastForward;
        outcome.commitId = theirs;
        return outcome;
    }

    const int baseId = mergeBase(head_, theirs);
    const Commit::Files none;
    const Commit::Files& baseFiles = baseId == 0 ? none : getCommit(baseId).getFiles();
    const Commit::Files& ourFiles = getCommit(head_).getFiles();
    const Commit::Files& theirFiles = getCommit(theirs).getFiles();
    std::set<std::string> paths;
    for (const Commit::Files* files : {&baseFiles, &ourFiles, &theirFiles}) {
        for (const auto& entry : *files) {
            paths.insert(entry.first);
        }
    }

    // A missing file reads as nullptr; equal pointers or equal contents mean "same".
    auto find = [](const Commit::Files& files, const std::string& path) -> const Snapshot* {
        auto found = files.find(path);
        return found == files.end() ? nullptr : &found->second;
    };
    auto same = [](const Snapshot* a, const Snapshot* b) {
        return a == b || (a && b && a->getContent() == b->getContent());
    };

    Commit::Files merged;
    for (const std::string& path : paths) {
        const Snapshot* base = find(baseFiles, path);
        const Snapshot* ours = find(ourFiles, path);
        const Snapshot* their = find(theirFiles, path);
        const Snapshot* result = nullptr;
        if (same(ours, their) || same(base, their)) {
            result = ours;
        } else if (same(base, ours)) {
            result = their;
        } else if (!ours || !their) {
            const Snapshot* kept = ours ? ours : their;
            Conflict conflict;
            conflict.location = "the whole file";
            conflict.base = base ? base->getContent() : "";
            conflict.ours = ours ? ours->getContent() : "";
            conflict.theirs = their ? their->getContent() : "";
            conflict.explanation = std::string(ours ? "This branch changed the file and \"" + branch + "\" deleted it."
                                                    : "This branch deleted the file and \"" + branch + "\" changed it.") +
                                   " The changed version was kept; delete it or commit it.";
            outcome.conflicts.push_back({path, conflict});
            merged.emplace(path, *kept);
            continue;
        } else {
            const Snapshot empty(path, "");
            MergeResult fileResult = mergeStrategyFor(path)->merge(base ? *base : empty, *ours, *their);
            for (const Conflict& conflict : fileResult.conflicts) {
                outcome.conflicts.push_back({path, conflict});
            }
            merged.emplace(path, Snapshot(path, std::move(fileResult.mergedContent)));
            continue;
        }
        if (result) {
            merged.emplace(path, *result);
        }
    }

    const std::string message = "Merge branch \"" + branch + "\" into " +
                                (branch_.empty() ? "commit #" + std::to_string(head_) : "\"" + branch_ + "\"");
    restoreFiles(merged);
    if (!outcome.conflicts.empty()) {
        pending_ = PendingMerge{theirs, branch};
        storage_.savePendingMerge(pending_);
        outcome.kind = MergeOutcome::Kind::Conflicts;
        return outcome;
    }
    outcome.kind = MergeOutcome::Kind::Merged;
    outcome.commitId = saveNewCommit(message, author, std::move(merged), theirs).getId();
    return outcome;
}

void Repository::abortMerge() {
    if (!isMerging()) {
        throw MergeConflictException("There is no merge to abort");
    }
    restoreFiles(head_ == 0 ? Commit::Files{} : getCommit(head_).getFiles());
    pending_ = PendingMerge{};
    storage_.savePendingMerge(pending_);
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

const Branch& Repository::findBranch(const std::string& name) const {
    for (const auto& entry : branches_) {
        if (Branch::sameNameIgnoringCase(entry.first, name)) {
            return entry.second;
        }
    }
    throw BranchNotFoundException("There is no branch named \"" + name + "\"");
}

void Repository::switchBranch(const std::string& name) {
    requireNoPendingMerge();
    const Branch& branch = findBranch(name);
    requireNoUncommittedChanges();
    const int id = branch.getCommitId();
    restoreFiles(id == 0 ? Commit::Files{} : getCommit(id).getFiles());
    storage_.saveHead(HeadRef{branch.getName(), 0});
    branch_ = branch.getName();
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

std::unique_ptr<MergeStrategy> Repository::mergeStrategyFor(const std::string&) {
    return std::make_unique<LineMerge>();
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
    requireNoPendingMerge();
    const Commit& target = getCommit(id);
    requireNoUncommittedChanges();
    restoreFiles(target.getFiles());
    storage_.saveHead(HeadRef{"", id});
    branch_.clear();
    head_ = id;
}

const Commit& Repository::revert(int id, const std::string& author) {
    requireNoPendingMerge();
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

void Repository::requireNoPendingMerge() const {
    if (isMerging()) {
        throw MergeConflictException("A merge with \"" + pending_.branch +
                                     "\" is waiting. Commit it or run chrono merge --abort first.");
    }
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
