#include "TestHarness.h"

#include <string>

#include "core/LCSDiff.h"

namespace {

DiffResult diffOf(const std::string& before, const std::string& after) {
    const LCSDiff algorithm;
    const DiffAlgorithm& diff = algorithm;  // used through the interface, as Repository does
    return diff.diff(Snapshot("f.txt", before), Snapshot("f.txt", after));
}

}  // namespace

// Issue #11
TEST(diffOfIdenticalTextIsEmpty) {
    CHECK(diffOf("a\nb\nc\n", "a\nb\nc\n").changes.empty());
    CHECK(diffOf("", "").changes.empty());
}

TEST(diffExplainsAChangeToOnlyTheFinalNewline) {
    const DiffResult result = diffOf("a\nb", "a\nb\n");
    CHECK(result.changes.size() == 1);
    CHECK(result.changes[0].location == "end of file (only the final newline changed)");
    CHECK(result.changes[0].before.empty());
    CHECK(result.changes[0].after.empty());
}

TEST(diffFindsAChangedLine) {
    const DiffResult result = diffOf("a\nb\nc\n", "a\nB\nc\n");
    CHECK(result.changes.size() == 1);
    CHECK(result.changes[0].location == "line 2");
    CHECK(result.changes[0].before == "b\n");
    CHECK(result.changes[0].after == "B\n");
}

TEST(diffFindsAddedAndRemovedLines) {
    const DiffResult added = diffOf("a\nc\n", "a\nb\nc\n");
    CHECK(added.changes.size() == 1);
    CHECK(added.changes[0].location == "after line 1");
    CHECK(added.changes[0].before.empty());
    CHECK(added.changes[0].after == "b\n");

    const DiffResult removed = diffOf("a\nb\nc\n", "a\nc\n");
    CHECK(removed.changes.size() == 1);
    CHECK(removed.changes[0].location == "line 2");
    CHECK(removed.changes[0].before == "b\n");
    CHECK(removed.changes[0].after.empty());

    const DiffResult atStart = diffOf("b\n", "a\nb\n");
    CHECK(atStart.changes.size() == 1);
    CHECK(atStart.changes[0].location == "at the start");
}

TEST(diffGroupsNeighbouringLines) {
    const DiffResult result = diffOf("1\n2\n3\n4\n5\n", "1\nX\nY\n4\n5\nZ\n");
    CHECK(result.changes.size() == 2);
    CHECK(result.changes[0].location == "lines 2-3");
    CHECK(result.changes[0].before == "2\n3\n");
    CHECK(result.changes[0].after == "X\nY\n");
    CHECK(result.changes[1].location == "after line 5");
    CHECK(result.changes[1].after == "Z\n");
}

TEST(diffKeepsTheLongestCommonLines) {
    // "a b c d" -> "b c d a": keeping b, c, d (3 lines) beats keeping only a.
    const DiffResult result = diffOf("a\nb\nc\nd\n", "b\nc\nd\na\n");
    CHECK(result.changes.size() == 2);
    CHECK(result.changes[0].before == "a\n");
    CHECK(result.changes[0].after.empty());
    CHECK(result.changes[1].before.empty());
    CHECK(result.changes[1].after == "a\n");
}

TEST(diffTellsEmptyLinesFromNothing) {
    const DiffResult result = diffOf("a\nb\n", "a\n\nb\n");
    CHECK(result.changes.size() == 1);
    CHECK(result.changes[0].before.empty());
    CHECK(result.changes[0].after == "\n");
}

TEST(diffHandlesWholeFilesAndLargeInput) {
    CHECK(diffOf("", "new\nfile\n").changes[0].after == "new\nfile\n");
    CHECK(diffOf("old\n", "").changes[0].before == "old\n");

    std::string before;
    std::string after;
    for (int i = 0; i < 5000; ++i) {
        before += "line " + std::to_string(i) + "\n";
        after += (i % 2 == 0 ? "changed " : "line ") + std::to_string(i) + "\n";
    }
    const DiffResult result = diffOf(before, after);
    CHECK(!result.changes.empty());
}
