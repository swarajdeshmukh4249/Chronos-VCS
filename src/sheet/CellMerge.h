#pragma once

#include <string>

#include "common/MergeStrategy.h"

// Three-way merge of a CSV sheet, cell by cell. Each side's rows are lined up
// with base by content, so a row inserted on one side and a cell edited on the
// other both survive. Within a row both sides kept:
//   - a cell only one side changed takes that change;
//   - a cell both sides changed the same way takes it once;
//   - a cell both sides changed differently is a conflict, written in the cell as
//       <<<<<<< ours: 1300 ======= theirs: 1250 >>>>>>>
//     so the rest of the file stays a CSV that Excel opens.
// Rows added on both sides at the same place are all kept (ours first), since
// two people adding entries is the normal case for a sheet; but two added rows
// with the same first field ("Travel") and different values are reported as a
// conflict, because that is often one item added twice. A row deleted on one
// side and edited on the other is a conflict that keeps the edited row.
//
// Untouched cells keep their exact text, and the result uses ours' line endings
// and byte order mark, so a merge never rewrites what nobody changed.
class CellMerge : public MergeStrategy {
public:
    MergeResult merge(const Snapshot& base, const Snapshot& ours, const Snapshot& theirs) const override;

    // The text a conflicting cell holds until someone picks a value.
    static std::string conflictCell(const std::string& ours, const std::string& theirs);
};
