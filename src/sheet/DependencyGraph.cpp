#include "sheet/DependencyGraph.h"

#include <deque>
#include <string>

#include "common/Exceptions.h"
#include "sheet/FormulaParser.h"

namespace {

const std::vector<CellRange> kNoRanges;
const std::vector<CellRef> kNoCells;

// "A1 -> B1 -> A1". A cycle of more than 8 cells shows only its first 3 and last 2:
// "A1 -> A2 -> A3 -> ... -> A9 -> A10 -> A1 (10 cells)".
std::string describeCycle(const std::vector<CellRef>& cycle) {
    const std::size_t kHead = 3;
    const std::size_t kTail = 2;
    const bool shorten = cycle.size() > 8;
    std::string text = "Circular reference: ";
    for (std::size_t i = 0; i < cycle.size(); ++i) {
        if (shorten && i == kHead) {
            text += "... -> ";
            i = cycle.size() - kTail;
        }
        text += cycle[i].toString() + " -> ";
    }
    text += cycle.front().toString();
    if (shorten) {
        text += " (" + std::to_string(cycle.size()) + " cells)";
    }
    return text;
}

}  // namespace

DependencyGraph::DependencyGraph(const Grid<Cell>& grid) {
    for (const auto& [ref, cell] : grid) {
        if (!cell.isFormula()) {
            continue;
        }
        std::vector<CellRange> references;
        try {
            FormulaParser::parse(cell.getText())->collectReferences(references);
        } catch (const ChronoException&) {
            // Evaluating the cell reports the syntax error.
        }
        std::vector<CellRange>& unique = dependencies_[ref];
        std::set<CellRange> seen;
        for (const CellRange& reference : references) {
            if (seen.insert(reference).second) {
                unique.push_back(reference);
            }
        }
    }

    // Every formula cell is known now, so each range can be resolved to the formulas inside it.
    for (const auto& [ref, ranges] : dependencies_) {
        std::vector<CellRef>& inputs = inputs_[ref];
        std::set<CellRef> seen;
        for (const CellRange& range : ranges) {
            if (range.isSingle()) {
                cellReaders_[range.topLeft].push_back(ref);
                if (seen.insert(range.topLeft).second) {
                    inputs.push_back(range.topLeft);
                }
                continue;
            }
            rangeReaders_.emplace_back(range, ref);
            for (const CellRef& formula : keysInRange(dependencies_, range)) {
                if (seen.insert(formula).second) {
                    inputs.push_back(formula);
                }
            }
        }
    }
}

const std::vector<CellRange>& DependencyGraph::dependenciesOf(const CellRef& ref) const {
    auto it = dependencies_.find(ref);
    return it == dependencies_.end() ? kNoRanges : it->second;
}

std::vector<CellRef> DependencyGraph::dependentsOf(const CellRef& ref) const {
    std::set<CellRef> readers;
    auto it = cellReaders_.find(ref);
    if (it != cellReaders_.end()) {
        readers.insert(it->second.begin(), it->second.end());
    }
    for (const auto& [range, reader] : rangeReaders_) {
        if (range.contains(ref)) {
            readers.insert(reader);
        }
    }
    return std::vector<CellRef>(readers.begin(), readers.end());
}

std::set<CellRef> DependencyGraph::affectedBy(const CellRef& ref) const {
    std::set<CellRef> affected;
    std::deque<CellRef> queue{ref};
    while (!queue.empty()) {
        const CellRef current = queue.front();
        queue.pop_front();
        for (const CellRef& dependent : dependentsOf(current)) {
            if (affected.insert(dependent).second) {
                queue.push_back(dependent);
            }
        }
    }
    return affected;
}

const std::vector<CellRef>& DependencyGraph::inputsOf(const CellRef& ref) const {
    auto it = inputs_.find(ref);
    return it == inputs_.end() ? kNoCells : it->second;
}

std::vector<CellRef> DependencyGraph::evaluationOrder(const CellRef& cell, const DonePredicate& alreadyDone) const {
    std::set<CellRef> finished;
    std::vector<CellRef> order;
    visit(cell, alreadyDone, finished, order);
    return order;
}

std::vector<CellRef> DependencyGraph::evaluationOrder() const {
    std::set<CellRef> finished;
    std::vector<CellRef> order;
    for (const auto& entry : dependencies_) {
        visit(entry.first, nullptr, finished, order);
    }
    return order;
}

void DependencyGraph::visit(const CellRef& start, const DonePredicate& alreadyDone, std::set<CellRef>& finished,
                            std::vector<CellRef>& order) const {
    auto skip = [&](const CellRef& ref) {
        return finished.count(ref) || !hasFormula(ref) || (alreadyDone && alreadyDone(ref));
    };

    struct Frame {
        CellRef ref;
        bool expanded;
    };
    std::vector<Frame> stack{{start, false}};
    // Expanded cells whose dependencies aren't finished yet: the current DFS path.
    std::set<CellRef> onPath;

    while (!stack.empty()) {
        const CellRef ref = stack.back().ref;
        if (skip(ref)) {
            stack.pop_back();
            continue;
        }

        if (!stack.back().expanded) {
            stack.back().expanded = true;
            onPath.insert(ref);
            for (const CellRef& dependency : inputsOf(ref)) {
                if (skip(dependency)) {
                    continue;
                }
                if (onPath.count(dependency)) {
                    // The expanded frames, bottom to top, are the path; the cycle starts at dependency.
                    std::vector<CellRef> cycle;
                    for (const Frame& frame : stack) {
                        if (frame.expanded && (!cycle.empty() || frame.ref == dependency)) {
                            cycle.push_back(frame.ref);
                        }
                    }
                    throw CircularReferenceException(describeCycle(cycle));
                }
                stack.push_back({dependency, false});
            }
            continue;
        }

        stack.pop_back();
        onPath.erase(ref);
        finished.insert(ref);
        order.push_back(ref);
    }
}
