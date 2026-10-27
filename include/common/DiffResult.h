#pragma once

#include <string>
#include <vector>

// location is "line 3" for text files or "B3" for spreadsheets.
struct Change {
    std::string location, before, after;
};

struct DiffResult {
    std::vector<Change> changes;
};
