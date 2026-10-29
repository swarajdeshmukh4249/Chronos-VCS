#include "core/LogGraph.h"

#include <algorithm>

#include "core/Repository.h"

namespace {

// One mark per column, e.g. "| * |". mark(j) gives column j's character.
template <typename Mark>
std::string row(std::size_t columns, Mark mark) {
    std::string text;
    for (std::size_t j = 0; j < columns; ++j) {
        text += (j == 0 ? "" : " ") + std::string(1, mark(j));
    }
    return text;
}

}  // namespace

std::vector<std::string> LogGraph::draw(const Repository& repo) {
    const std::vector<Branch> branches = repo.getBranches();
    std::vector<std::string> lines;
    // lanes[j] is the commit column j is waiting for.
    std::vector<int> lanes;
    // A parent always has a smaller id than its child, so newest first is safe.
    for (int id = static_cast<int>(repo.log().size()); id >= 1; --id) {
        const Commit& commit = repo.getCommit(id);
        std::vector<std::size_t> mine;
        for (std::size_t j = 0; j < lanes.size(); ++j) {
            if (lanes[j] == id) {
                mine.push_back(j);
            }
        }
        if (mine.empty()) {
            lanes.push_back(id);  // a branch tip nothing newer leads to
            mine.push_back(lanes.size() - 1);
        }
        const std::size_t at = mine.front();

        // Other columns that were waiting for this commit join it first.
        if (mine.size() > 1) {
            lines.push_back(row(lanes.size(), [&mine](std::size_t j) {
                return std::find(mine.begin() + 1, mine.end(), j) != mine.end() ? '/' : '|';
            }));
            for (auto it = mine.rbegin(); it + 1 != mine.rend(); ++it) {
                lanes.erase(lanes.begin() + static_cast<std::ptrdiff_t>(*it));
            }
        }

        std::string labels;
        if (id == repo.getHead()) {
            labels += "HEAD";
        }
        for (const Branch& branch : branches) {
            if (branch.getCommitId() == id) {
                labels += (labels.empty() ? "" : ", ") + branch.getName();
            }
        }
        const bool merge = commit.getMergedParentId() != 0;
        std::string graph = row(lanes.size(), [at](std::size_t j) { return j == at ? '*' : '|'; });
        graph.resize(std::max<std::size_t>(graph.size(), merge ? 3 : 1), ' ');
        lines.push_back(graph + " #" + std::to_string(id) + "  " + commit.getMessage() +
                        (labels.empty() ? "" : "  (" + labels + ")"));

        if (commit.hasParent()) {
            lanes[at] = commit.getParentId();
        } else {
            lanes.erase(lanes.begin() + static_cast<std::ptrdiff_t>(at));
        }
        // A merge opens a column for the branch it brought in.
        if (merge) {
            lanes.insert(lanes.begin() + static_cast<std::ptrdiff_t>(at) + 1, commit.getMergedParentId());
            lines.push_back(row(lanes.size(), [at](std::size_t j) { return j == at + 1 ? '\\' : '|'; }));
        }
    }
    return lines;
}
