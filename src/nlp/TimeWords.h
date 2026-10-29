#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "nlp/Tokenizer.h"

// The stretch of time a phrase picks out, as [from, until) in local time.
// "yesterday" is a whole day, "yesterday evening" only its evening hours, and
// "2 hours ago" the hour around that moment. phrase keeps the words it was read
// from, so a message can quote the user back ("nothing was saved yesterday evening").
struct TimeWindow {
    std::time_t from = 0;
    std::time_t until = 0;
    std::string phrase;

    bool found() const { return until > from; }
    bool contains(std::time_t when) const { return when >= from && when < until; }
};

// Reads plain-English times. Days run from midnight to midnight in local time,
// and the parts of a day are morning 06:00-12:00, afternoon 12:00-17:00,
// evening 17:00-22:00 and night 22:00-06:00, which is how people use the words
// even though nothing makes them exact.
class TimeWords {
public:
    // The first time phrase in tokens, or an empty window when there is none.
    // now anchors the relative words, so tests can fix "today".
    //
    // Understands: today, yesterday, tonight, last night, this morning, a
    // weekday name ("Monday", "last Friday"), a part of a day on its own or
    // after a day ("yesterday evening"), "N <unit> ago" for minutes, hours,
    // days, weeks, months and years ("a week ago" counts as one), "last
    // week/month/year", a clock time ("5pm", "17:30") and an ISO date
    // ("2026-10-20"). A bare number is not a time: "revert 3" means commit 3.
    static TimeWindow read(const std::vector<Token>& tokens, std::time_t now);

    // True for a length of time such as "hours" or "days", so whoever reads
    // commit numbers knows the 3 in "3 days ago" is a count, not commit 3.
    static bool isLengthOfTime(const std::string& word);
};
