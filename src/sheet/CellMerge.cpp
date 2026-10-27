#include "sheet/CellMerge.h"

#include <algorithm>
#include <string>
#include <vector>

#include "sheet/CSVHandler.h"
#include "sheet/CellRef.h"
#include "sheet/RowAlignment.h"

namespace {

using Row = CSVHandler::Row;
using Rows = CSVHandler::Rows;

const char* const kByteOrderMark = "\xEF\xBB\xBF";

// What one side did to base: where each base row went, and which rows it added.
struct SideChanges {
    std::vector<std::size_t> rowFor;              // per base row: the side's row, or kNone if deleted
    std::vector<std::vector<std::size_t>> added;  // per gap (before base row i, or at the end): new rows
};

SideChanges changesFrom(const Rows& base, const Rows& side) {
    SideChanges changes;
    changes.rowFor.assign(base.size(), RowMatch::kNone);
    changes.added.resize(base.size() + 1);
    std::size_t nextBase = 0;
    for (const RowMatch& match : alignRows(base, side)) {
        if (match.oldRow == RowMatch::kNone) {
            changes.added[nextBase].push_back(match.newRow);
        } else {
            changes.rowFor[match.oldRow] = match.newRow;
            nextBase = match.oldRow + 1;
        }
    }
    return changes;
}

std::string display(const std::string& value) {
    return value.empty() ? "(empty)" : value;
}

std::string rowText(const Row& row) {
    return CSVHandler::formatRows({row}, "");
}

std::string lineEndOf(const std::string& csv) {
    if (csv.find("\r\n") != std::string::npos) {
        return "\r\n";
    }
    return csv.find('\n') != std::string::npos ? "\n" : "\r\n";
}

class Merger {
public:
    Merger(const Rows& base, const Rows& ours, const Rows& theirs)
        : base_(base), ours_(ours), theirs_(theirs), ourChanges_(changesFrom(base, ours)),
          theirChanges_(changesFrom(base, theirs)) {}

    MergeResult run(const std::string& lineEnd) {
        for (std::size_t i = 0; i <= base_.size(); ++i) {
            addRows(ourChanges_.added[i], theirChanges_.added[i]);
            if (i < base_.size()) {
                mergeBaseRow(i);
            }
        }
        result_.mergedContent = CSVHandler::formatRows(merged_, lineEnd);
        return result_;
    }

private:
    void addRows(const std::vector<std::size_t>& ourRows, const std::vector<std::size_t>& theirRows) {
        for (std::size_t row : ourRows) {
            merged_.push_back(ours_[row]);
        }
        for (std::size_t row : theirRows) {
            // Both sides adding the same row (say, the same new expense) keeps one copy.
            const bool duplicate = std::any_of(ourRows.begin(), ourRows.end(),
                                               [&](std::size_t ourRow) { return sameRow(ours_[ourRow], theirs_[row]); });
            if (!duplicate) {
                merged_.push_back(theirs_[row]);
            }
        }
    }

    void mergeBaseRow(std::size_t i) {
        const std::size_t ourRow = ourChanges_.rowFor[i];
        const std::size_t theirRow = theirChanges_.rowFor[i];
        if (ourRow == RowMatch::kNone && theirRow == RowMatch::kNone) {
            return;
        }
        if (ourRow == RowMatch::kNone || theirRow == RowMatch::kNone) {
            const bool weDeleted = ourRow == RowMatch::kNone;
            const Row& kept = weDeleted ? theirs_[theirRow] : ours_[ourRow];
            if (sameRow(kept, base_[i])) {
                return;  // deleted on one side, untouched on the other
            }
            Conflict conflict;
            conflict.location = "row " + std::to_string(merged_.size() + 1);
            conflict.base = rowText(base_[i]);
            conflict.ours = weDeleted ? "" : rowText(kept);
            conflict.theirs = weDeleted ? rowText(kept) : "";
            conflict.explanation = std::string(weDeleted ? "This branch deleted " : "The other branch deleted ") +
                                   conflict.location + " and the " + (weDeleted ? "other branch" : "this branch") +
                                   " changed it. The changed row was kept; delete it or commit it.";
            result_.conflicts.push_back(conflict);
            merged_.push_back(kept);
            return;
        }
        merged_.push_back(mergeCells(base_[i], ours_[ourRow], theirs_[theirRow]));
    }

    Row mergeCells(const Row& base, const Row& ours, const Row& theirs) {
        const std::size_t width = std::max(ours.size(), theirs.size());
        Row row(width);
        for (std::size_t col = 0; col < width; ++col) {
            const std::string& b = fieldAt(base, col);
            const std::string& o = fieldAt(ours, col);
            const std::string& t = fieldAt(theirs, col);
            if (o == t || t == b) {
                row[col] = o;
            } else if (o == b) {
                row[col] = t;
            } else {
                Conflict conflict;
                conflict.location = CellRef{static_cast<int>(merged_.size()), static_cast<int>(col)}.toString();
                conflict.base = b;
                conflict.ours = o;
                conflict.theirs = t;
                conflict.explanation = "Both versions changed " + conflict.location + " in different ways: this branch to " +
                                       display(o) + ", the other branch to " + display(t) + " (it was " + display(b) + ").";
                result_.conflicts.push_back(conflict);
                row[col] = CellMerge::conflictCell(o, t);
            }
        }
        return row;
    }

    const Rows& base_;
    const Rows& ours_;
    const Rows& theirs_;
    SideChanges ourChanges_;
    SideChanges theirChanges_;
    Rows merged_;
    MergeResult result_;
};

}  // namespace

std::string CellMerge::conflictCell(const std::string& ours, const std::string& theirs) {
    return "<<<<<<< ours: " + display(ours) + " ======= theirs: " + display(theirs) + " >>>>>>>";
}

MergeResult CellMerge::merge(const Snapshot& base, const Snapshot& ours, const Snapshot& theirs) const {
    // When at most one side changed the file, its text is the answer, byte for byte.
    MergeResult result;
    if (ours.getContent() == theirs.getContent() || base.getContent() == theirs.getContent()) {
        result.mergedContent = ours.getContent();
        return result;
    }
    if (base.getContent() == ours.getContent()) {
        result.mergedContent = theirs.getContent();
        return result;
    }

    const Rows baseRows = CSVHandler::parseRows(base.getContent());
    const Rows ourRows = CSVHandler::parseRows(ours.getContent());
    const Rows theirRows = CSVHandler::parseRows(theirs.getContent());
    result = Merger(baseRows, ourRows, theirRows).run(lineEndOf(ours.getContent()));
    if (ours.getContent().rfind(kByteOrderMark, 0) == 0) {
        result.mergedContent = kByteOrderMark + result.mergedContent;
    }
    return result;
}
