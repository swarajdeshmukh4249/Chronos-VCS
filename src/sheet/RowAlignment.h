#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "sheet/CSVHandler.h"

// One step in lining up two versions of a sheet, in order. A row both versions
// have (unchanged or edited) sets both indexes; a removed row has no newRow and
// an added row has no oldRow.
struct RowMatch {
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    std::size_t oldRow = kNone;
    std::size_t newRow = kNone;
};

// True when the rows hold the same fields, ignoring empty fields at the end, so
// "a,b" and "a,b," are the same row.
bool sameRow(const CSVHandler::Row& a, const CSVHandler::Row& b);

// The field at col, or "" past the end of the row.
const std::string& fieldAt(const CSVHandler::Row& row, std::size_t col);

// Lines the rows up by content (longest common subsequence), so a row inserted in
// the middle is one added row instead of every row below it looking changed.
// Inside a run of changed rows, removed and added rows are paired in order as
// edited rows; any extra rows are plain removals or additions.
std::vector<RowMatch> alignRows(const CSVHandler::Rows& oldRows, const CSVHandler::Rows& newRows);
