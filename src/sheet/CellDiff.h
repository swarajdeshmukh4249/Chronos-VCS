#pragma once

#include "common/DiffAlgorithm.h"

// Cell-by-cell diff of two CSV versions of a sheet, e.g. "B3: 40 -> 55".
//
// Rows are lined up by content first, so inserting a row reports one added row
// instead of every cell below it. Each Change is one of:
//   an edited cell:  location "B3", before "40", after "55" (empty before or after
//                    means the cell was filled in or cleared)
//   an added row:    location "row 4", before "", after the row as CSV
//   a removed row:   location "row 4", before the row as CSV, after ""
// Cell and added-row locations use the new version's row numbers; removed rows
// use the old version's. Cells are compared as written, so "1.5" -> "1.50" counts.
class CellDiff : public DiffAlgorithm {
public:
    DiffResult diff(const Snapshot& a, const Snapshot& b) const override;
};
