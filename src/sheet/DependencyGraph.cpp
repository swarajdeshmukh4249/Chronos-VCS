#include "sheet/DependencyGraph.h"

#include <deque>
#include <string>

#include "common/Exceptions.h"
#include "sheet/FormulaParser.h"

namespace {

const std::vector<CellRef> kNone;

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
        std::vector<CellRef> references;
        try {
            FormulaParser::parse(cell.getText())->collectReferences(references);
        } catch (const ChronoException&) {
            // Evaluating the cell reports the syntax error.
        }
        std::vector<CellRef>& unique = dependencies_[ref];
        std::set<CellRef> seen;
        for (const CellRef& reference : references) {
            if (seen.insert(reference).second) {
                unique.push_back(reference);
                dependents_[reference].push_back(ref);
            }
        }
    }
}

const std::vector<CellRef>& DependencyGraph::dependenciesOf(const CellRef& ref) const {
    auto it = dependencies_.find(ref);
    return it == dependencies_.end() ? kNone : it->second;
}

const std::vector<CellRef>& DependencyGraph::dependentsOf(const CellRef& ref) const {
    auto it = dependents_.find(ref);
    return it == dependents_.end() ? kNone : it->second;
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
            for (const CellRef& dependency : dependenciesOf(ref)) {
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
