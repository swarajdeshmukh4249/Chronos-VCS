#include "TestHarness.h"

#include <string>

#include "core/LineMerge.h"

namespace {

MergeResult mergeOf(const std::string& base, const std::string& ours, const std::string& theirs) {
    const LineMerge strategy;
    const MergeStrategy& merge = strategy;  // used through the interface, as Repository does
    return merge.merge(Snapshot("f.txt", base), Snapshot("f.txt", ours), Snapshot("f.txt", theirs));
}

}  // namespace

// Issue #13
TEST(mergeCombinesChangesToDifferentLines) {
    const MergeResult result = mergeOf("Rent 1000\nFood 300\nTravel 150\nGym 0\n",
                                       "Rent 1100\nFood 300\nTravel 150\nGym 0\n",
                                       "Rent 1000\nFood 300\nTravel 150\nGym 40\n");
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == "Rent 1100\nFood 300\nTravel 150\nGym 40\n");
}

TEST(mergeTakesOneSidedChanges) {
    CHECK(mergeOf("a\nb\n", "a\nB\n", "a\nb\n").mergedContent == "a\nB\n");
    CHECK(mergeOf("a\nb\n", "a\nb\n", "a\nB\n").mergedContent == "a\nB\n");
    CHECK(mergeOf("a\nb\n", "a\nb\n", "a\nb\n").mergedContent == "a\nb\n");
}

TEST(mergeAcceptsTheSameChangeOnBothSides) {
    const MergeResult result = mergeOf("a\nb\nc\n", "a\nB\nc\n", "a\nB\nc\n");
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == "a\nB\nc\n");
}

TEST(mergeReportsConflictingChangesWithMarkers) {
    const MergeResult result = mergeOf("Rent 1000\nFood 300\nGym 0\n",
                                       "Rent 1000\nFood 350\nGym 0\n",
                                       "Rent 1000\nFood 250\nGym 0\n");
    CHECK(result.conflicts.size() == 1);
    const Conflict& conflict = result.conflicts[0];
    CHECK(conflict.location == "line 2");
    CHECK(conflict.base == "Food 300\n");
    CHECK(conflict.ours == "Food 350\n");
    CHECK(conflict.theirs == "Food 250\n");
    CHECK(conflict.explanation == "Both versions changed line 2 in different ways.");
    CHECK(result.mergedContent ==
          "Rent 1000\n<<<<<<< ours\nFood 350\n=======\nFood 250\n>>>>>>> theirs\nGym 0\n");
}

TEST(mergeTreatsNeighbouringChangesAsAConflict) {
    // Like Git: edits to lines right next to each other can't be combined safely.
    const MergeResult result = mergeOf("a\nb\nc\nd\n", "a\nB\nc\nd\n", "a\nb\nC\nd\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].location == "lines 2-3");
    CHECK(result.conflicts[0].ours == "B\nc\n");
    CHECK(result.conflicts[0].theirs == "b\nC\n");
}

TEST(mergeReportsDifferentAdditionsInTheSamePlace) {
    const MergeResult result = mergeOf("a\nb\n", "a\nb\nfrom ours\n", "a\nb\nfrom theirs\n");
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].location == "after line 2");
    CHECK(result.conflicts[0].explanation == "Both versions added different lines after line 2.");
}

TEST(mergeCombinesDeletionsAndEditsElsewhere) {
    const MergeResult result = mergeOf("1\n2\n3\n4\n5\n", "1\n3\n4\n5\n", "1\n2\n3\n4\nFIVE\n");
    CHECK(result.conflicts.empty());
    CHECK(result.mergedContent == "1\n3\n4\nFIVE\n");
}

TEST(mergeKeepsSeveralConflictsApart) {
    const MergeResult result = mergeOf("1\n2\n3\n4\n5\n", "one\n2\n3\n4\nfive\n", "uno\n2\n3\n4\ncinco\n");
    CHECK(result.conflicts.size() == 2);
    CHECK(result.conflicts[0].location == "line 1");
    CHECK(result.conflicts[1].location == "line 5");
}

TEST(mergeHandlesMissingFinalNewline) {
    const MergeResult clean = mergeOf("a\nb", "A\nb", "a\nb");
    CHECK(clean.mergedContent == "A\nb");

    const MergeResult conflict = mergeOf("a\nb", "a\nB", "a\nbb");
    CHECK(conflict.conflicts.size() == 1);
    CHECK(conflict.mergedContent == "a\n<<<<<<< ours\nB\n=======\nbb\n>>>>>>> theirs\n");
}

TEST(mergeOfFilesAddedOnBothSides) {
    CHECK(mergeOf("", "same\n", "same\n").conflicts.empty());
    CHECK(mergeOf("", "ours\n", "theirs\n").conflicts.size() == 1);
}
