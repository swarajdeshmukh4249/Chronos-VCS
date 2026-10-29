#include "TestHarness.h"

#include <ctime>
#include <string>

#include "nlp/TimeWords.h"
#include "nlp/Tokenizer.h"

namespace {

// Local time, so the windows line up with the ones TimeWords builds.
std::time_t at(int year, int month, int day, int hour, int minute = 0) {
    std::tm parts{};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_isdst = -1;
    return std::mktime(&parts);
}

// Wednesday 28 October 2026, early afternoon.
const std::time_t kNow = at(2026, 10, 28, 14, 0);

TimeWindow windowOf(const std::string& request) {
    return TimeWords::read(Tokenizer::tokenize(request), kNow);
}

int weekdayOf(std::time_t when) {
    const std::tm* parts = std::localtime(&when);
    return parts ? parts->tm_wday : -1;
}

}  // namespace

// Issue #17
TEST(yesterdayEveningIsYesterdaysEveningHours) {
    const TimeWindow window = windowOf("go back to how things were yesterday evening");
    CHECK(window.found());
    CHECK(window.from == at(2026, 10, 27, 17, 0));
    CHECK(window.until == at(2026, 10, 27, 22, 0));
    CHECK(window.phrase == "yesterday evening");
}

TEST(aDayOnItsOwnRunsMidnightToMidnight) {
    const TimeWindow yesterday = windowOf("restore yesterday's version");
    CHECK(yesterday.from == at(2026, 10, 27, 0, 0));
    CHECK(yesterday.until == at(2026, 10, 28, 0, 0));

    const TimeWindow today = windowOf("what changed today");
    CHECK(today.from == at(2026, 10, 28, 0, 0));
    CHECK(today.until == at(2026, 10, 29, 0, 0));
}

TEST(partsOfTheDayCoverTheUsualHours) {
    CHECK(windowOf("this morning").from == at(2026, 10, 28, 6, 0));
    CHECK(windowOf("this morning").until == at(2026, 10, 28, 12, 0));
    CHECK(windowOf("this afternoon").from == at(2026, 10, 28, 12, 0));
    CHECK(windowOf("this afternoon").until == at(2026, 10, 28, 17, 0));
    CHECK(windowOf("at noon").from == at(2026, 10, 28, 12, 0));
    CHECK(windowOf("at noon").until == at(2026, 10, 28, 13, 0));
}

TEST(aPartOfTheDayOnItsOwnMeansToday) {
    const TimeWindow window = windowOf("undo everything since this morning");
    CHECK(window.found());
    CHECK(window.from == at(2026, 10, 28, 6, 0));
}

TEST(nightRunsPastMidnightIntoTheNextMorning) {
    const TimeWindow tonight = windowOf("tonight");
    CHECK(tonight.from == at(2026, 10, 28, 22, 0));
    CHECK(tonight.until == at(2026, 10, 29, 6, 0));

    // "last night" is the night just gone, so it starts on yesterday.
    const TimeWindow lastNight = windowOf("the version from last night");
    CHECK(lastNight.from == at(2026, 10, 27, 22, 0));
    CHECK(lastNight.until == at(2026, 10, 28, 6, 0));
    CHECK(lastNight.phrase == "last night");
}

TEST(hoursAndMinutesAgoAreMomentsNotDays) {
    const TimeWindow window = windowOf("restore the version from 2 hours ago");
    CHECK(window.found());
    CHECK(window.from == kNow - 2 * 3600 - 1800);
    CHECK(window.until == kNow - 2 * 3600 + 1800);
    CHECK(window.phrase == "2 hours ago");

    const TimeWindow minutes = windowOf("undo what I did 20 minutes ago");
    CHECK(minutes.from == kNow - 20 * 60 - 1800);
}

TEST(daysWeeksAndMonthsAgoNameAWholeDay) {
    const TimeWindow days = windowOf("the version from 3 days ago");
    CHECK(days.from == at(2026, 10, 25, 0, 0));
    CHECK(days.until == at(2026, 10, 26, 0, 0));

    // No "ago" needed when a number was given.
    CHECK(windowOf("go back 3 days").from == at(2026, 10, 25, 0, 0));

    // "a week ago" loses its "a" as a filler, so a bare unit counts as one.
    CHECK(windowOf("a week ago").from == at(2026, 10, 21, 0, 0));
    CHECK(windowOf("last week").from == at(2026, 10, 21, 0, 0));
    CHECK(windowOf("two weeks ago").from == at(2026, 10, 14, 0, 0));
}

TEST(spelledOutNumbersCountToo) {
    CHECK(windowOf("three days ago").from == at(2026, 10, 25, 0, 0));
    CHECK(windowOf("a couple of hours ago").from == kNow - 2 * 3600 - 1800);
}

TEST(aWeekdayIsTheMostRecentOne) {
    const TimeWindow monday = windowOf("go back to Monday");
    CHECK(monday.found());
    CHECK(weekdayOf(monday.from) == 1);
    CHECK(monday.from < kNow);
    CHECK(kNow - monday.from < 7 * 24 * 3600);
    CHECK(monday.until == monday.from + 24 * 3600);

    // Said on a Wednesday, "last Wednesday" is the one before today.
    const TimeWindow lastWednesday = windowOf("last Wednesday");
    CHECK(weekdayOf(lastWednesday.from) == 3);
    CHECK(lastWednesday.from == at(2026, 10, 21, 0, 0));

    // A weekday narrows the same way a date does.
    const TimeWindow mondayMorning = windowOf("Monday morning");
    CHECK(mondayMorning.from == monday.from + 6 * 3600);
    CHECK(mondayMorning.until == monday.from + 12 * 3600);
}

TEST(isoDatesAndClockTimesAreUnderstood) {
    const TimeWindow date = windowOf("check out 2026-10-20");
    CHECK(date.from == at(2026, 10, 20, 0, 0));
    CHECK(date.until == at(2026, 10, 21, 0, 0));

    const TimeWindow dated = windowOf("how it looked on 2026-10-20 in the evening");
    CHECK(dated.from == at(2026, 10, 20, 17, 0));
    CHECK(dated.until == at(2026, 10, 20, 22, 0));

    const TimeWindow afternoon = windowOf("the version from 5pm");
    CHECK(afternoon.from == at(2026, 10, 28, 17, 0));
    CHECK(afternoon.until == at(2026, 10, 28, 18, 0));

    CHECK(windowOf("at 9am").from == at(2026, 10, 28, 9, 0));
    CHECK(windowOf("at 17:30").from == at(2026, 10, 28, 17, 30));
    // Midnight and noon are the two the 12-hour clock gets wrong most often.
    CHECK(windowOf("at 12am").from == at(2026, 10, 28, 0, 0));
    CHECK(windowOf("at 12pm").from == at(2026, 10, 28, 12, 0));
}

TEST(aClockTimeCanSitOnANamedDay) {
    const TimeWindow window = windowOf("how it looked yesterday at 17:30");
    CHECK(window.from == at(2026, 10, 27, 17, 30));
    CHECK(window.until == at(2026, 10, 27, 18, 30));
}

TEST(requestsWithNoTimeHaveNoWindow) {
    // The whole point: a bare number is a commit, not a time, or "revert 3"
    // would mean three o'clock in the morning.
    CHECK(!windowOf("revert 3").found());
    CHECK(!windowOf("roll it back to commit 2").found());
    CHECK(!windowOf("save my work").found());
    CHECK(!windowOf("switch to experiment").found());
    CHECK(!windowOf("which branches are there").found());
    CHECK(!windowOf("commit this as \"three days of fixes\"").found());
}

TEST(aWindowKnowsWhatFallsInsideIt) {
    const TimeWindow window = windowOf("yesterday evening");
    CHECK(window.contains(at(2026, 10, 27, 18, 30)));
    CHECK(window.contains(at(2026, 10, 27, 17, 0)));
    // The end is excluded, so windows can sit side by side without overlapping.
    CHECK(!window.contains(at(2026, 10, 27, 22, 0)));
    CHECK(!window.contains(at(2026, 10, 27, 16, 59)));
}

TEST(lengthsOfTimeAreToldApartFromOtherWords) {
    CHECK(TimeWords::isLengthOfTime("days"));
    CHECK(TimeWords::isLengthOfTime("hour"));
    CHECK(TimeWords::isLengthOfTime("weeks"));
    CHECK(!TimeWords::isLengthOfTime("commit"));
    CHECK(!TimeWords::isLengthOfTime("monday"));
    CHECK(!TimeWords::isLengthOfTime("evening"));
}
