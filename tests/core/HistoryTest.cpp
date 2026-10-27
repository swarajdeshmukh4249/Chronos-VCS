#include "TestHarness.h"

#include <stdexcept>
#include <string>

#include "core/History.h"

// Issue #7
TEST(historyStartsEmpty) {
    History<int> history;
    CHECK(history.empty());
    CHECK(history.size() == 0);
    CHECK(!history.contains(1));
    CHECK_THROWS(history.latest(), std::out_of_range);
}

TEST(historyNumbersEntriesFromOne) {
    History<std::string> history;
    history.append("first");
    history.append("second");
    history.append("third");
    CHECK(history.size() == 3);
    CHECK(history.at(1) == "first");
    CHECK(history.at(3) == "third");
    CHECK(history.latest() == "third");
    CHECK(history.contains(3));
    CHECK(!history.contains(0));
    CHECK(!history.contains(4));
}

TEST(historyRejectsMissingEntries) {
    History<int> history;
    history.append(10);
    CHECK_THROWS(history.at(0), std::out_of_range);
    CHECK_THROWS(history.at(2), std::out_of_range);
}

TEST(historyIteratesOldestFirst) {
    History<int> history;
    for (int i = 1; i <= 4; ++i) {
        history.append(i * 10);
    }
    int expected = 10;
    for (int value : history) {
        CHECK(value == expected);
        expected += 10;
    }
}
