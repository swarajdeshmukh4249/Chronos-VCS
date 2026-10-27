#pragma once

#include <stdexcept>
#include <string>

class ChronoException : public std::runtime_error {
public:
    explicit ChronoException(const std::string& message)
        : std::runtime_error(message) {}
};

// --- Core / NLP layer (Swaraj) ---

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
