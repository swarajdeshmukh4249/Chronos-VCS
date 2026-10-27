#include "TestHarness.h"

#include "common/Exceptions.h"
#include "core/Branch.h"

// Issue #8
TEST(branchAcceptsSafeNames) {
    for (const char* name : {"main", "feature", "try-new-totals", "v1.2", "tanishka_csv", "A1"}) {
        CHECK(Branch::isValidName(name));
    }
}

TEST(branchRejectsUnsafeNames) {
    for (const char* name : {"", ".hidden", "-flag", "a/b", "../main", "has space", "tab\t", "semi;colon",
                             "123456789012345678901234567890123456789012345678901"}) {
        CHECK(!Branch::isValidName(name));
        CHECK_THROWS(Branch(name, 0), InvalidBranchNameException);
    }
}

TEST(branchMovesToNewCommits) {
    Branch branch("main", 0);
    CHECK(branch.getCommitId() == 0);
    branch.moveTo(4);
    CHECK(branch.getCommitId() == 4);
    CHECK(branch.getName() == "main");
}

TEST(branchNamesCompareIgnoringCase) {
    CHECK(Branch::sameNameIgnoringCase("main", "Main"));
    CHECK(Branch::sameNameIgnoringCase("Swaraj", "swaraj"));
    CHECK(!Branch::sameNameIgnoringCase("main", "mainline"));
}
