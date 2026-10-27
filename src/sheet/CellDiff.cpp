#include "sheet/CellDiff.h"

#include <algorithm>
#include <string>

#include "sheet/CSVHandler.h"
#include "sheet/CellRef.h"
#include "sheet/RowAlignment.h"

namespace {

std::string rowText(const CSVHandler::Row& row) {
    return CSVHandler::formatRows({row}, "");
}

std::string rowLocation(std::size_t row) {
    return "row " + std::to_string(row + 1);
}

}  // namespace

DiffResult CellDiff::diff(const Snapshot& a, const Snapshot& b) const {
    const CSVHandler::Rows oldRows = CSVHandler::parseRows(a.getContent());
    const CSVHandler::Rows newRows = CSVHandler::parseRows(b.getContent());

    DiffResult result;
    for (const RowMatch& match : alignRows(oldRows, newRows)) {
        if (match.newRow == RowMatch::kNone) {
            result.changes.push_back({rowLocation(match.oldRow), rowText(oldRows[match.oldRow]), ""});
        } else if (match.oldRow == RowMatch::kNone) {
            result.changes.push_back({rowLocation(match.newRow), "", rowText(newRows[match.newRow])});
        } else {
            const CSVHandler::Row& before = oldRows[match.oldRow];
            const CSVHandler::Row& after = newRows[match.newRow];
            const std::size_t width = std::max(before.size(), after.size());
            for (std::size_t col = 0; col < width; ++col) {
                if (fieldAt(before, col) != fieldAt(after, col)) {
                    const CellRef ref{static_cast<int>(match.newRow), static_cast<int>(col)};
                    result.changes.push_back({ref.toString(), fieldAt(before, col), fieldAt(after, col)});
                }
            }
        }
    }
    return result;
}
