#pragma once

#include "common/MergeStrategy.h"

// Three-way merge of text, line by line. Each side's changes are found by
// comparing it with base. Changes to different lines are combined. Changes to
// the same or neighbouring lines are a conflict, unless both sides made the
// same change, and are written with Git-style markers:
//   <<<<<<< ours
//   our lines
//   =======
//   their lines
//   >>>>>>> theirs
class LineMerge : public MergeStrategy {
public:
    MergeResult merge(const Snapshot& base, const Snapshot& ours, const Snapshot& theirs) const override;
};
