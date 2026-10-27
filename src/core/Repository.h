#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "common/DiffAlgorithm.h"
#include "common/MergeStrategy.h"
#include "core/Branch.h"
#include "core/Commit.h"
#include "core/History.h"
#include "core/StorageManager.h"
#include "core/WorkingTree.h"

// How one file differs between two versions of the project.
struct FileDiff {
    enum class Status { Added, Removed, Modified };

    std::string path;
    Status status;
    DiffResult result;
};

// What happened when a branch was merged in.
struct MergeOutcome {
    enum class Kind {
        AlreadyUpToDate,  // everything on the branch is already in HEAD
        FastForward,      // HEAD was behind, so it simply moved forward
        Merged,           // both sides had changes; a merge commit was made
        Conflicts         // files have conflict markers; fix them and commit
    };
    struct FileConflict {
        std::string path;
        Conflict conflict;
    };

    Kind kind = Kind::AlreadyUpToDate;
    int commitId = 0;  // HEAD afterwards, for FastForward and Merged
    std::vector<FileConflict> conflicts;
};

// A project's version history. StorageManager handles .chrono/ and WorkingTree
// handles the user's files, so this class only holds the version-control rules.
class Repository {
public:
    // Creates .chrono/ in root. Throws RepositoryExistsException.
    static Repository init(const std::filesystem::path& root);
    // Opens the repository that contains start. Throws RepositoryNotFoundException.
    static Repository open(const std::filesystem::path& start);

    const std::filesystem::path& getRoot() const { return tree_.getRoot(); }

    // Includes the file in every later commit and returns its path relative to
    // the root. Throws FileNotFoundException.
    std::string track(const std::filesystem::path& path);
    const std::set<std::string>& getTracked() const { return tracked_; }

    // Saves every tracked file as a new commit whose parent is the HEAD commit,
    // and moves the current branch to it. While a merge waits on conflicts, this
    // finishes it with a merge commit. Throws NothingToCommitException when
    // nothing is tracked or nothing changed, or MergeConflictException if a file
    // still has conflict markers.
    const Commit& commit(const std::string& message, const std::string& author);

    // The commit the working files came from, or 0 before the first commit.
    int getHead() const { return head_; }
    // The branch new commits go on, or "" after checking out a commit directly.
    const std::string& getCurrentBranch() const { return branch_; }

    // Every commit on every branch, oldest first.
    const History<Commit>& log() const { return commits_; }
    // Ids of the commits that led to HEAD, through both parents of merges, newest first.
    std::vector<int> historyOfHead() const;
    // Every commit reachable from id through either parent, including id itself.
    // Throws CommitNotFoundException.
    std::set<int> ancestorsOf(int id) const;
    // The newest commit both a and b grew from, or 0 if they share none. A parent
    // always has a smaller id than its child, so this is the common ancestor with
    // the highest id. Throws CommitNotFoundException.
    int mergeBase(int a, int b) const;
    // Throws CommitNotFoundException.
    const Commit& getCommit(int id) const;

    // Every branch, sorted by name.
    std::vector<Branch> getBranches() const;
    // Starts a branch at the HEAD commit without switching to it. Throws
    // InvalidBranchNameException, or BranchExistsException if a branch with the
    // same name ignoring case exists.
    const Branch& createBranch(const std::string& name);
    // Puts the branch's newest files on disk and makes it the current branch.
    // Throws BranchNotFoundException, or UncommittedChangesException rather than
    // overwrite unsaved work.
    void switchBranch(const std::string& name);

    // What changed from commit fromId to commit toId, one entry per changed file.
    // Throws CommitNotFoundException.
    std::vector<FileDiff> diff(int fromId, int toId) const;
    // What changed in the tracked files since the HEAD commit.
    std::vector<FileDiff> diffUncommitted() const;

    // True when a tracked file differs from the HEAD commit.
    bool hasUncommittedChanges() const;

    // Puts the files from commit id back on disk and makes it HEAD, leaving the
    // current branch. Throws CommitNotFoundException, or
    // UncommittedChangesException rather than overwrite unsaved work.
    void checkout(int id);

    // Puts the files from commit id back and records that as a new commit on top
    // of HEAD, so no history is lost. Throws like checkout, or
    // NothingToCommitException if the files already match.
    const Commit& revert(int id, const std::string& author);

    // Brings the branch's changes into HEAD: a fast-forward when HEAD is simply
    // behind, otherwise a three-way merge against the commit where they split.
    // On conflicts the files are written with markers and nothing is committed
    // until the user fixes them and commits. Throws BranchNotFoundException,
    // UncommittedChangesException, or MergeConflictException if a merge is
    // already waiting.
    MergeOutcome merge(const std::string& branch, const std::string& author);
    // Memento pattern: a snapshot of HEAD, branches, tracked files, any waiting
    // merge and the working files, taken before a command runs.
    RepositoryState captureState(const std::string& description) const;
    // Adds a snapshot to the undo history, after its command succeeded.
    void rememberForUndo(const RepositoryState& state);
    // What `undo` would take back, e.g. "commit \"fix totals\"", or "" if nothing.
    std::string nextUndo() const;
    // Puts everything back as it was before the last remembered command and
    // returns that command's description. Commits it took back stay in the
    // history (`log --all`), so nothing is lost. Throws NothingToUndoException,
    // or UncommittedChangesException if files were edited and aren't saved in
    // any commit.
    std::string undo();

    // True while a merge waits for its conflicts to be fixed and committed.
    bool isMerging() const { return pending_.commitId != 0; }
    const PendingMerge& getPendingMerge() const { return pending_; }
    // Puts the files back as they were before the merge. Throws
    // MergeConflictException if no merge is waiting.
    void abortMerge();

private:
    explicit Repository(const std::filesystem::path& root);

    // The tracked files as they are on disk right now. Deleted files are left out.
    Commit::Files currentFiles() const;
    bool differsFromHead(const Commit::Files& files) const;
    // Throws UncommittedChangesException.
    void requireNoUncommittedChanges() const;
    // Writes these files and removes tracked files that aren't among them.
    void restoreFiles(const Commit::Files& files);
    // Points HEAD, and the current branch if there is one, at commit id.
    void moveHeadTo(int id);
    // Records files as a new commit on top of HEAD and moves HEAD to it.
    const Commit& saveNewCommit(const std::string& message, const std::string& author, Commit::Files files,
                                int mergedParentId);

    static std::vector<FileDiff> compare(const Commit::Files& before, const Commit::Files& after);
    // Strategy pattern: how to compare one file. Phase 3 adds a cell-by-cell diff for .csv files.
    static std::unique_ptr<DiffAlgorithm> diffAlgorithmFor(const std::string& path);
    // Strategy pattern: how to merge one file. #15 sends .csv files to a cell-by-cell merge.
    static std::unique_ptr<MergeStrategy> mergeStrategyFor(const std::string& path);

    WorkingTree tree_;
    StorageManager storage_;
    History<Commit> commits_;
    std::set<std::string> tracked_;
    std::map<std::string, Branch> branches_;
    std::string branch_;
    int head_ = 0;
    PendingMerge pending_;
};
