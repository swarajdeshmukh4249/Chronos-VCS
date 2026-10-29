#include "nlp/TimeWords.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

#include "nlp/IntentMatcher.h"

namespace {

constexpr std::time_t kMinute = 60;
constexpr std::time_t kHour = 60 * kMinute;

// Half the width of the window a single moment ("2 hours ago") stands for, so a
// commit saved a few minutes either side of it still counts as that moment.
constexpr std::time_t kMomentReach = 30 * kMinute;

std::tm localParts(std::time_t when) {
    const std::tm* local = std::localtime(&when);
    return local ? *local : std::tm{};
}

// Midnight starting the day that is dayOffset days from the one holding when.
// mktime normalises out-of-range fields, so it copes with month ends, and
// tm_isdst = -1 lets it work out daylight saving for itself.
std::time_t startOfDay(std::time_t when, int dayOffset = 0) {
    std::tm parts = localParts(when);
    parts.tm_mday += dayOffset;
    parts.tm_hour = 0;
    parts.tm_min = 0;
    parts.tm_sec = 0;
    parts.tm_isdst = -1;
    return std::mktime(&parts);
}

// A clock time on the day holding day. hour may run past 24 to mean the next
// morning, which is how "night" is written below.
std::time_t atClock(std::time_t day, int hour, int minute = 0) {
    std::tm parts = localParts(day);
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = 0;
    parts.tm_isdst = -1;
    return std::mktime(&parts);
}

bool isDigits(const std::string& word) {
    return !word.empty() && std::all_of(word.begin(), word.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

// A count written as digits or as a word. "a week ago" loses its "a" as a
// filler, so a unit with no number in front of it counts as one.
bool readCount(const std::string& word, int& count) {
    static const std::map<std::string, int> spelled = {
        {"one", 1},   {"two", 2},  {"three", 3},   {"four", 4},     {"five", 5},
        {"six", 6},   {"seven", 7}, {"eight", 8},  {"nine", 9},     {"ten", 10},
        {"eleven", 11}, {"twelve", 12}, {"couple", 2}, {"few", 3},
    };
    if (isDigits(word)) {
        count = std::stoi(word);
        return true;
    }
    const auto found = spelled.find(word);
    if (found == spelled.end()) {
        return false;
    }
    count = found->second;
    return true;
}

enum class Unit { None, Minute, Hour, Day, Week, Month, Year };

Unit readUnit(const std::string& word) {
    static const std::map<std::string, Unit> units = {
        {"minute", Unit::Minute}, {"minutes", Unit::Minute}, {"min", Unit::Minute}, {"mins", Unit::Minute},
        {"hour", Unit::Hour},     {"hours", Unit::Hour},     {"hr", Unit::Hour},    {"hrs", Unit::Hour},
        {"day", Unit::Day},       {"days", Unit::Day},       {"week", Unit::Week},  {"weeks", Unit::Week},
        {"month", Unit::Month},   {"months", Unit::Month},   {"year", Unit::Year},  {"years", Unit::Year},
    };
    const auto found = units.find(word);
    return found == units.end() ? Unit::None : found->second;
}

// Sunday is 0, to line up with std::tm::tm_wday.
int readWeekday(const std::string& word) {
    static const std::map<std::string, int> days = {
        {"sunday", 0},    {"sun", 0}, {"monday", 1},   {"mon", 1}, {"tuesday", 2}, {"tue", 2}, {"tues", 2},
        {"wednesday", 3}, {"wed", 3}, {"thursday", 4}, {"thu", 4}, {"thurs", 4},   {"friday", 5}, {"fri", 5},
        {"saturday", 6},  {"sat", 6},
    };
    const auto found = days.find(word);
    return found == days.end() ? -1 : found->second;
}

// The hours a part of a day covers. Night runs past midnight into the next
// morning, so its end is written as 30 rather than 6.
struct DayPart {
    int fromHour;
    int untilHour;
};

bool readDayPart(const std::string& word, DayPart& part) {
    static const std::map<std::string, DayPart> parts = {
        {"morning", {6, 12}}, {"afternoon", {12, 17}}, {"evening", {17, 22}}, {"night", {22, 30}},
        {"noon", {12, 13}},   {"midday", {12, 13}},    {"midnight", {0, 1}},
    };
    const auto found = parts.find(word);
    if (found == parts.end()) {
        return false;
    }
    part = found->second;
    return true;
}

// "5pm", "5am", "17:30". A bare number is deliberately not a clock time, so
// "revert 3" stays a commit number rather than becoming 03:00.
bool readClock(std::string word, int& hour, int& minute) {
    bool afternoon = false;
    bool halfDayMark = false;
    if (word.size() > 2) {
        const std::string suffix = word.substr(word.size() - 2);
        if (suffix == "am" || suffix == "pm") {
            afternoon = suffix == "pm";
            halfDayMark = true;
            word.resize(word.size() - 2);
        }
    }
    const std::size_t colon = word.find(':');
    minute = 0;
    if (colon != std::string::npos) {
        const std::string hourPart = word.substr(0, colon);
        const std::string minutePart = word.substr(colon + 1);
        if (!isDigits(hourPart) || !isDigits(minutePart)) {
            return false;
        }
        hour = std::stoi(hourPart);
        minute = std::stoi(minutePart);
    } else {
        if (!halfDayMark || !isDigits(word)) {
            return false;
        }
        hour = std::stoi(word);
    }
    if (halfDayMark) {
        if (hour == 12) {
            hour = afternoon ? 12 : 0;
        } else if (afternoon) {
            hour += 12;
        }
    }
    return hour >= 0 && hour < 24 && minute >= 0 && minute < 60;
}

// "2026-10-20". Gives back midnight starting that day.
bool readIsoDate(const std::string& word, std::time_t& day) {
    if (word.size() != 10 || word[4] != '-' || word[7] != '-') {
        return false;
    }
    const std::string year = word.substr(0, 4);
    const std::string month = word.substr(5, 2);
    const std::string dayOfMonth = word.substr(8, 2);
    if (!isDigits(year) || !isDigits(month) || !isDigits(dayOfMonth)) {
        return false;
    }
    std::tm parts{};
    parts.tm_year = std::stoi(year) - 1900;
    parts.tm_mon = std::stoi(month) - 1;
    parts.tm_mday = std::stoi(dayOfMonth);
    parts.tm_isdst = -1;
    day = std::mktime(&parts);
    return day != static_cast<std::time_t>(-1);
}

std::string joinWords(const std::vector<std::string>& words) {
    std::string joined;
    for (const std::string& word : words) {
        if (!joined.empty()) {
            joined += ' ';
        }
        joined += word;
    }
    return joined;
}

}  // namespace

bool TimeWords::isLengthOfTime(const std::string& word) {
    return readUnit(word) != Unit::None;
}

TimeWindow TimeWords::read(const std::vector<Token>& tokens, std::time_t now) {
    const std::vector<std::string> words = IntentMatcher::keyWords(tokens);

    // A moment counted back from now: "2 hours ago", "20 minutes ago". A moment
    // can't be narrowed by a part of a day, so these answer on their own.
    // Bigger units name a whole day instead, handled in the same loop.
    for (std::size_t i = 0; i < words.size(); ++i) {
        int count = 1;
        std::size_t unitAt = i;
        std::vector<std::string> said;
        const bool counted = readCount(words[i], count);
        if (counted || words[i] == "last") {
            unitAt = i + 1;
            said.push_back(words[i]);
        }
        // "a couple of hours ago" puts an "of" between the count and the unit.
        if (unitAt > i && unitAt < words.size() && words[unitAt] == "of") {
            said.push_back(words[unitAt]);
            ++unitAt;
        }
        if (unitAt >= words.size() || count <= 0) {
            continue;
        }
        const Unit unit = readUnit(words[unitAt]);
        if (unit == Unit::None) {
            continue;
        }
        said.push_back(words[unitAt]);
        // "ago" or "back" rounds off "2 hours ago", but a counted phrase stands
        // on its own too ("go back 3 days"), and so does "last week". Only a
        // bare unit needs the tail, so the "week" in "which week" isn't a time.
        const bool trailed = unitAt + 1 < words.size() && (words[unitAt + 1] == "ago" || words[unitAt + 1] == "back");
        if (trailed) {
            said.push_back(words[unitAt + 1]);
        } else if (!counted && words[i] != "last") {
            continue;
        }
        if (unit == Unit::Minute || unit == Unit::Hour) {
            const std::time_t step = unit == Unit::Minute ? kMinute : kHour;
            const std::time_t moment = now - count * step;
            return {moment - kMomentReach, moment + kMomentReach, joinWords(said)};
        }
        // A day, week, month or year back names the whole of that day: "the
        // version from three days ago" means how things stood that day. A month
        // counts as 30 days and a year as 365, because people say them loosely.
        const int perUnit = unit == Unit::Day ? 1 : unit == Unit::Week ? 7 : unit == Unit::Month ? 30 : 365;
        const std::time_t day = startOfDay(now, -count * perUnit);
        return {day, startOfDay(day, 1), joinWords(said)};
    }

    // Otherwise a day, narrowed by the part of it or the clock time named anywhere.
    std::time_t day = 0;
    bool namedDay = false;
    DayPart part{0, 24};
    bool namedPart = false;
    std::vector<std::string> said;

    for (std::size_t i = 0; i < words.size() && !namedDay; ++i) {
        const std::string& word = words[i];
        std::time_t isoDay = 0;
        const int weekday = readWeekday(word);
        if (word == "today") {
            day = startOfDay(now);
            namedDay = true;
        } else if (word == "yesterday") {
            day = startOfDay(now, -1);
            namedDay = true;
        } else if (word == "tonight") {
            day = startOfDay(now);
            part = {22, 30};
            namedDay = true;
            namedPart = true;
        } else if (readIsoDate(word, isoDay)) {
            day = isoDay;
            namedDay = true;
        } else if (weekday >= 0) {
            // The most recent such weekday, counting today. "last Friday" said
            // on a Friday means the Friday before it.
            const std::tm nowParts = localParts(now);
            int back = (nowParts.tm_wday - weekday + 7) % 7;
            if (back == 0 && i > 0 && words[i - 1] == "last") {
                back = 7;
            }
            day = startOfDay(now, -back);
            namedDay = true;
        }
        if (namedDay) {
            if (i > 0 && words[i - 1] == "last") {
                said.push_back(words[i - 1]);
            }
            said.push_back(word);
        }
    }

    for (std::size_t i = 0; i < words.size() && !namedPart; ++i) {
        DayPart found{};
        int hour = 0;
        int minute = 0;
        if (readDayPart(words[i], found)) {
            if (words[i] == "night" && i > 0 && words[i - 1] == "last") {
                // "last night" is the night just gone, so it hangs off
                // yesterday even though its hours run into this morning.
                day = startOfDay(now, -1);
                namedDay = true;
                said = {"last", "night"};
            } else {
                if (!namedDay) {
                    day = startOfDay(now);
                    namedDay = true;
                }
                said.push_back(words[i]);
            }
            part = found;
            namedPart = true;
        } else if (readClock(words[i], hour, minute)) {
            if (!namedDay) {
                day = startOfDay(now);
                namedDay = true;
            }
            said.push_back(words[i]);
            const std::time_t from = atClock(day, hour, minute);
            return {from, from + kHour, joinWords(said)};
        }
    }

    if (!namedDay) {
        return {};
    }
    return {atClock(day, part.fromHour), atClock(day, part.untilHour), joinWords(said)};
}
