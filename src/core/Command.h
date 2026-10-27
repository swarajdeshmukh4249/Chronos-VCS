#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "core/Repository.h"

// Command pattern: each action that changes the repository is an object. The
// command line builds one with CommandFactory (and in Phase 4, so will the
// plain-English parser), then runs it. run() takes a snapshot first, so every
// command can be taken back with `undo` without writing its own undo code.
class Command {
public:
    virtual ~Command() = default;

    // What the command does, in plain English, e.g. "commit \"fix totals\"".
    virtual std::string describe() const = 0;

    // Template method: snapshot, execute, then remember the snapshot for undo.
    // Returns the exit code for the command line (0 for success).
    int run(Repository& repo, std::ostream& out);

protected:
    virtual int execute(Repository& repo, std::ostream& out) = 0;
    // False for commands that shouldn't be undone themselves, such as undo.
    virtual bool canBeUndone() const { return true; }
};

class AddCommand : public Command {
public:
    explicit AddCommand(std::vector<std::string> paths) : paths_(std::move(paths)) {}
    std::string describe() const override;

protected:
    int execute(Repository& repo, std::ostream& out) override;

private:
    std::vector<std::string> paths_;
};

class CommitCommand : public Command {
public:
    CommitCommand(std::string message, std::string author)
        : message_(std::move(message)), author_(std::move(author)) {}
    std::string describe() const override;

protected:
    int execute(Repository& repo, std::ostream& out) override;

private:
    std::string message_;
    std::string author_;
};

class CheckoutCommand : public Command {
public:
    explicit CheckoutCommand(int commitId) : commitId_(commitId) {}
    std::string describe() const override;

protected:
    int execute(Repository& repo, std::ostream& out) override;

private:
    int commitId_;
};

class RevertCommand : public Command {
public:
    RevertCommand(int commitId, std::string author) : commitId_(commitId), author_(std::move(author)) {}
    std::string describe() const override;

protected:
    int execute(Repository& repo, std::ostream& out) override;

private:
    int commitId_;
    std::string author_;
};

class CreateBranchCommand : public Command {
public:
    explicit CreateBranchCommand(std::string name) : name_(std::move(name)) {}
    std::string describe() const override;

protected:
    int execute(Repository& repo, std::ostream& out) override;

private:
    std::string name_;
};

class SwitchCommand : public Command {
public:
    explicit SwitchCommand(std::string name) : name_(std::move(name)) {}
    std::string describe() const override;

protected:
    int execute(Repository& repo, std::ostream& out) override;

private:
    std::string name_;
};

class MergeCommand : public Command {
public:
    MergeCommand(std::string branch, std::string author) : branch_(std::move(branch)), author_(std::move(author)) {}
    std::string describe() const override;

protected:
    int execute(Repository& repo, std::ostream& out) override;

private:
    std::string branch_;
    std::string author_;
};

class AbortMergeCommand : public Command {
public:
    std::string describe() const override;

protected:
    int execute(Repository& repo, std::ostream& out) override;
};

class UndoCommand : public Command {
public:
    std::string describe() const override;

protected:
    int execute(Repository& repo, std::ostream& out) override;
    bool canBeUndone() const override { return false; }
};

// Factory pattern: turns command-line words into the matching Command.
class CommandFactory {
public:
    // e.g. {"commit", "-m", "fix totals"} -> CommitCommand. Returns nullptr for
    // words that don't name a command that changes the repository; read-only
    // commands such as log and diff are handled by the command line itself.
    // Throws CommitNotFoundException for a commit number that isn't a number.
    static std::unique_ptr<Command> create(const std::vector<std::string>& args, const std::string& author);

    // "12" -> 12. Throws CommitNotFoundException for anything else.
    static int parseCommitId(const std::string& text);
};
