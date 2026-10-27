#pragma once

#include <stdexcept>
#include <string>

class ChronoException : public std::runtime_error {
public:
    explicit ChronoException(const std::string& message)
        : std::runtime_error(message) {}
};

// --- Core / NLP layer (Swaraj) ---

// No .chrono/ folder in this directory or any parent.
class RepositoryNotFoundException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

class RepositoryExistsException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

class CommitNotFoundException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

// Nothing is tracked, or nothing changed since the last commit.
class NothingToCommitException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

class BranchNotFoundException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

// A branch with that name, ignoring case, already exists.
class BranchExistsException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

class InvalidBranchNameException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

class NothingToUndoException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

// A merge is waiting to be finished, or a file still has conflict markers.
class MergeConflictException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

// Checking out would overwrite edits that were never committed.
class UncommittedChangesException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

// A file the user named does not exist in the working directory.
class FileNotFoundException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

// A file under .chrono/ is missing, unreadable or malformed.
class CorruptObjectException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

// --- Spreadsheet layer (Tanishka) ---

class CircularReferenceException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

class InvalidCellReferenceException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

class DivisionByZeroException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

class SemanticConflictException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

class FormulaSyntaxException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

// A formula used text where a number was needed, e.g. ="abc"+1.
class TypeMismatchException : public ChronoException {
public:
    using ChronoException::ChronoException;
};

// A result too large for a double, e.g. =A1*10 where A1 is 1e308.
class NumberOverflowException : public ChronoException {
public:
    using ChronoException::ChronoException;
};
