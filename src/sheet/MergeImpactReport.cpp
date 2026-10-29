#include "sheet/MergeImpactReport.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>

#include "sheet/CSVHandler.h"
#include "sheet/RowAlignment.h"
#include "sheet/SheetEvaluator.h"
#include "sheet/Value.h"

namespace {

// For each merged row, the same row in rows, or kNone.
std::vector<std::size_t> rowMap(const CSVHandler::Rows& rows, const CSVHandler::Rows& merged) {
    std::vector<std::size_t> map(merged.size(), RowMatch::kNone);
    for (const RowMatch& match : alignRows(rows, merged)) {
        if (match.oldRow != RowMatch::kNone && match.newRow != RowMatch::kNone) {
            map[match.newRow] = match.oldRow;
        }
    }
    return map;
}

std::string valueAt(const SheetEvaluator& evaluator, const CellRef& ref) {
    try {
        const std::string value = valueToString(evaluator.valueAt(ref));
        return value.empty() ? "(empty)" : value;
    } catch (const std::exception& e) {
        return std::string("error: ") + e.what();
    }
}

// One version of the sheet, read through the merged sheet's row numbers, since a
// row inserted above can move a row down on one side only.
class Side {
public:
    Side(const std::string& csv, const CSVHandler::Rows& merged)
        : rows_(CSVHandler::parseRows(csv)), grid_(CSVHandler::parse(csv)), evaluator_(grid_),
          map_(rowMap(rows_, merged)) {}

    // The base, whose rows may look nothing like the merged ones when both sides
    // edited them, so each is found through ours or, failing that, theirs.
    Side(const std::string& csv, const Side& ours, const Side& theirs)
        : rows_(CSVHandler::parseRows(csv)), grid_(CSVHandler::parse(csv)), evaluator_(grid_),
          map_(ours.map_.size(), RowMatch::kNone) {
        const std::vector<std::size_t> viaOurs = rowMap(rows_, ours.rows_);
        const std::vector<std::size_t> viaTheirs = rowMap(rows_, theirs.rows_);
        for (std::size_t row = 0; row < map_.size(); ++row) {
            if (ours.hasRow(row)) {
                map_[row] = viaOurs[ours.map_[row]];
            }
            if (map_[row] == RowMatch::kNone && theirs.hasRow(row)) {
                map_[row] = viaTheirs[theirs.map_[row]];
            }
        }
    }

    bool hasRow(std::size_t mergedRow) const { return map_[mergedRow] != RowMatch::kNone; }

    std::string text(std::size_t mergedRow, std::size_t col) const {
        return hasRow(mergedRow) ? fieldAt(rows_[map_[mergedRow]], col) : std::string();
    }

    std::size_t width(std::size_t mergedRow) const { return hasRow(mergedRow) ? rows_[map_[mergedRow]].size() : 0; }

    std::string value(std::size_t mergedRow, std::size_t col) const {
        if (!hasRow(mergedRow)) {
            return "(no such row)";
        }
        return valueAt(evaluator_, CellRef{static_cast<int>(map_[mergedRow]), static_cast<int>(col)});
    }

private:
    CSVHandler::Rows rows_;
    Grid<Cell> grid_;
    SheetEvaluator evaluator_;
    std::vector<std::size_t> map_;
};

// "A1", "A1 and B2", "A1, B2 and C3".
std::string joined(const std::vector<std::string>& names) {
    std::string text;
    for (std::size_t i = 0; i < names.size(); ++i) {
        text += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    }
    return text;
}

}  // namespace

MergeImpactReport::MergeImpactReport(const std::string& base, const std::string& ours, const std::string& theirs,
                                     const std::string& merged) {
    const Grid<Cell> mergedGrid = CSVHandler::parse(merged);
    if (std::none_of(mergedGrid.begin(), mergedGrid.end(), [](const auto& entry) { return entry.second.isFormula(); })) {
        return;
    }
    const CSVHandler::Rows mergedRows = CSVHandler::parseRows(merged);
    const SheetEvaluator evaluator(mergedGrid);
    const Side ourSide(ours, mergedRows);
    const Side theirSide(theirs, mergedRows);
    const Side baseSide(base, ourSide, theirSide);

    // In a clean merge every cell comes from ours or theirs, so a cell that differs
    // from one side was changed by the other.
    std::set<CellRef> byOurs;
    std::set<CellRef> byTheirs;
    for (std::size_t row = 0; row < mergedRows.size(); ++row) {
        const std::size_t width = std::max({mergedRows[row].size(), ourSide.width(row), theirSide.width(row)});
        for (std::size_t col = 0; col < width; ++col) {
            const std::string& mine = fieldAt(mergedRows[row], col);
            const CellRef ref{static_cast<int>(row), static_cast<int>(col)};
            if (mine != theirSide.text(row, col) && mine == ourSide.text(row, col)) {
                byOurs.insert(ref);
            } else if (mine != ourSide.text(row, col) && mine == theirSide.text(row, col)) {
                byTheirs.insert(ref);
            }
        }
    }

    // For each formula cell, the changed cells it reads, directly or not.
    const DependencyGraph& graph = evaluator.getGraph();
    auto readersOf = [&graph](const std::set<CellRef>& changed) {
        std::map<CellRef, std::vector<std::string>> readers;
        for (const CellRef& cell : changed) {
            for (const CellRef& reader : graph.affectedBy(cell)) {
                readers[reader].push_back(cell.toString());
            }
        }
        return readers;
    };
    const auto readsOurs = readersOf(byOurs);
    const auto readsTheirs = readersOf(byTheirs);

    for (const auto& [ref, cell] : mergedGrid) {
        if (!cell.isFormula()) {
            continue;
        }
        const auto row = static_cast<std::size_t>(ref.row);
        const auto col = static_cast<std::size_t>(ref.col);
        CellImpact impact;
        impact.cell = ref;
        impact.merged = valueAt(evaluator, ref);
        impact.ours = ourSide.value(row, col);
        impact.theirs = theirSide.value(row, col);
        if (impact.merged == impact.ours && impact.merged == impact.theirs) {
            continue;
        }
        impact.base = baseSide.value(row, col);

        const bool newToBoth = impact.merged != impact.ours && impact.merged != impact.theirs;
        const auto ourInputs = readsOurs.find(ref);
        const auto theirInputs = readsTheirs.find(ref);
        if (newToBoth && byTheirs.count(ref) != 0 && ourInputs != readsOurs.end()) {
            impact.semantic = true;
            impact.explanation = ref.toString() + " (changed on the other branch) reads " + joined(ourInputs->second) +
                                 " (changed on this branch).";
        } else if (newToBoth && byOurs.count(ref) != 0 && theirInputs != readsTheirs.end()) {
            impact.semantic = true;
            impact.explanation = ref.toString() + " (changed on this branch) reads " + joined(theirInputs->second) +
                                 " (changed on the other branch).";
        }
        if (impact.semantic) {
            impact.explanation += " Merged, it is " + impact.merged + ", a value neither branch saw: this branch had " +
                                  impact.ours + " and the other branch " + impact.theirs + ".";
        }
        impacts_.push_back(impact);
    }
}

std::vector<CellImpact> MergeImpactReport::semanticConflicts() const {
    std::vector<CellImpact> conflicts;
    std::copy_if(impacts_.begin(), impacts_.end(), std::back_inserter(conflicts),
                 [](const CellImpact& impact) { return impact.semantic; });
    return conflicts;
}

std::vector<std::string> MergeImpactReport::lines() const {
    std::vector<std::string> lines;
    for (const CellImpact& impact : impacts_) {
        lines.push_back(impact.cell.toString() + ": before " + impact.base + ", this branch " + impact.ours +
                        ", the other branch " + impact.theirs + ", merged " + impact.merged +
                        (impact.semantic ? "  (semantic conflict)" : ""));
    }
    return lines;
}
