#include "TestHarness.h"

#include "core/ContentHash.h"

// Issue #9. Expected values are the published FNV-1a 64-bit test vectors.
TEST(contentHashMatchesKnownValues) {
    CHECK(contentHash("") == "cbf29ce484222325");
    CHECK(contentHash("a") == "af63dc4c8601ec8c");
    CHECK(contentHash("foobar") == "85944171f73967e8");
}

TEST(contentHashSeesSmallChanges) {
    CHECK(contentHash("Budget v1\n") == contentHash("Budget v1\n"));
    CHECK(contentHash("Budget v1\n") != contentHash("Budget v2\n"));
    CHECK(contentHash("A1,10") != contentHash("A1,10\n"));
}

TEST(isContentHashChecksTheForm) {
    CHECK(isContentHash(contentHash("anything")));
    CHECK(isContentHash("0123456789abcdef"));
    CHECK(!isContentHash("0123456789ABCDEF"));
    CHECK(!isContentHash("0123456789abcde"));
    CHECK(!isContentHash("0123456789abcdeg"));
    CHECK(!isContentHash("../../etc/passwd"));
}
