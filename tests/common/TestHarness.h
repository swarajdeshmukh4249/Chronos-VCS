#pragma once

#include <iostream>
#include <vector>

// Minimal stand-in until the team picks a test framework (e.g. GoogleTest).
namespace harness {

struct TestCase {
    const char* name;
    void (*run)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

inline void reportFailure(const char* file, int line, const char* what) {
    ++failures();
    std::cerr << file << ":" << line << ": FAILED: " << what << "\n";
}

}  // namespace harness

#define TEST(name)                                              \
    static void name();                                         \
    static harness::Registrar name##_registrar(#name, name);    \
    static void name()

#define CHECK(expr)                                                   \
    do {                                                              \
        if (!(expr)) harness::reportFailure(__FILE__, __LINE__, #expr); \
    } while (0)

#define CHECK_THROWS(expr, ExceptionType)                                              \
    do {                                                                               \
        bool thrown = false;                                                           \
        try {                                                                          \
            (void)(expr);                                                              \
        } catch (const ExceptionType&) {                                               \
            thrown = true;                                                             \
        } catch (...) {                                                                \
        }                                                                              \
        if (!thrown) harness::reportFailure(__FILE__, __LINE__, #expr " throws " #ExceptionType); \
    } while (0)
