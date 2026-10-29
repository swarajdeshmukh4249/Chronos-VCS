#include "core/HtmlView.h"

#include <algorithm>
#include <ctime>
#include <exception>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "core/Repository.h"
#include "sheet/CSVHandler.h"
#include "sheet/MergeImpactReport.h"
#include "sheet/SheetEvaluator.h"
#include "sheet/Value.h"

namespace {

// Big sheets are cut down to this, so the page stays quick to open.
constexpr int kMaxRows = 60;
constexpr int kMaxCols = 26;

std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string when(std::time_t time) {
    char text[32] = "";
    if (const std::tm* local = std::localtime(&time)) {
        std::strftime(text, sizeof text, "%Y-%m-%d %H:%M", local);
    }
    return text;
}

std::string columnName(int col) { return CellRef{0, col}.toString().substr(0, CellRef{0, col}.toString().size() - 1); }

std::string chip(std::size_t count, const char* one, const char* many) {
    return "<span class=\"chip\"><b>" + std::to_string(count) + "</b> " + (count == 1 ? one : many) + "</span>";
}

// A merged sheet value neither side saw, found again from the commits themselves.
struct Paradox {
    int commitId;
    std::string path;
    CellImpact impact;
};

std::vector<Paradox> findParadoxes(const Repository& repo) {
    std::vector<Paradox> paradoxes;
    for (const Commit& commit : repo.log()) {
        if (commit.getMergedParentId() == 0) {
            continue;
        }
        const Commit& ours = repo.getCommit(commit.getParentId());
        const Commit& theirs = repo.getCommit(commit.getMergedParentId());
        const int baseId = repo.mergeBase(ours.getId(), theirs.getId());
        for (const auto& [path, snapshot] : commit.getFiles()) {
            if (!Repository::isSheet(path)) {
                continue;
            }
            auto contentIn = [&path = path](const Commit::Files& files) {
                const auto found = files.find(path);
                return found == files.end() ? std::string() : found->second.getContent();
            };
            const std::string base = baseId == 0 ? std::string() : contentIn(repo.getCommit(baseId).getFiles());
            try {
                const MergeImpactReport report(base, contentIn(ours.getFiles()), contentIn(theirs.getFiles()),
                                               snapshot.getContent());
                for (const CellImpact& impact : report.semanticConflicts()) {
                    paradoxes.push_back({commit.getId(), path, impact});
                }
            } catch (const std::exception&) {
                // A sheet that no longer parses has no values to compare.
            }
        }
    }
    return paradoxes;
}

void writeStyle(std::ostream& out) {
    out << R"(<style>
:root{--bg:#0d1324;--panel:#151d35;--panel2:#1c2645;--ink:#e8e3d3;--dim:#8f96ad;--brass:#e0b45a;
--brass2:#f5d48f;--teal:#4fd1c5;--add:#3ecf8e;--del:#ff6b81;--paradox:#ff4d6d;--line:#2b365c}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(1200px 600px at 80% -10%,#1d2a55 0,var(--bg) 60%);
background-color:var(--bg);background-attachment:fixed;min-height:100vh;color:var(--ink);font:15px/1.5 "Segoe UI",system-ui,sans-serif}
.wrap{max-width:1100px;margin:0 auto;padding:28px 16px 60px}
header{display:flex;gap:22px;align-items:center;flex-wrap:wrap;margin-bottom:26px}
.dial{width:84px;height:84px;border-radius:50%;flex:none;position:relative;
background:conic-gradient(var(--brass) 0 25%,#3a3320 25% 100%);box-shadow:0 0 30px #e0b45a55}
.dial::before{content:"";position:absolute;inset:7px;border-radius:50%;background:var(--bg)}
.dial::after{content:"";position:absolute;left:50%;top:14px;width:3px;height:28px;background:var(--brass2);
transform-origin:50% 100%;animation:tick 6s steps(12) infinite;border-radius:2px}
@keyframes tick{to{transform:rotate(360deg)}}
h1{font:700 34px/1.1 Georgia,serif;margin:0;letter-spacing:1px}h1 span{color:var(--brass)}
.tag{color:var(--dim);margin-top:4px}.stats{display:flex;gap:10px;flex-wrap:wrap;margin-top:10px}
.chip{background:var(--panel2);border:1px solid var(--line);border-radius:99px;padding:3px 12px;font-size:13px}
.chip b{color:var(--brass2)}
h2{font:700 20px Georgia,serif;color:var(--brass);margin:34px 0 12px;display:flex;align-items:center;gap:10px}
h2::after{content:"";flex:1;height:1px;background:linear-gradient(90deg,var(--line),transparent)}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:14px 16px;margin-bottom:12px}
.paradox{border-color:var(--paradox);background:linear-gradient(135deg,#3a1424,var(--panel) 70%);
animation:glow 2.4s ease-in-out infinite}
@keyframes glow{50%{box-shadow:0 0 24px #ff4d6d66}}
.paradox .big{font:700 22px Georgia,serif;color:var(--paradox)}
.vals{display:flex;gap:10px;flex-wrap:wrap;margin-top:8px}.vals div{background:var(--bg);border-radius:10px;
padding:6px 12px;border:1px solid var(--line)}.vals small{display:block;color:var(--dim);font-size:11px;
text-transform:uppercase;letter-spacing:1px}.vals .now{border-color:var(--paradox);color:var(--paradox);font-weight:700}
.ok{color:var(--add)}.stream{position:relative;padding-left:34px}
.stream::before{content:"";position:absolute;left:12px;top:6px;bottom:6px;width:3px;
background:linear-gradient(var(--brass),var(--teal));border-radius:2px}
.commit{position:relative}.commit::before{content:"";position:absolute;left:-29px;top:18px;width:13px;height:13px;
border-radius:50%;background:var(--bg);border:3px solid var(--brass)}
.commit.merge::before{border-color:var(--teal);transform:rotate(45deg);border-radius:3px}
.commit.head::before{background:var(--brass2);box-shadow:0 0 14px var(--brass)}
summary{cursor:pointer;list-style:none;display:flex;gap:10px;align-items:baseline;flex-wrap:wrap}
summary::-webkit-details-marker{display:none}.id{font:700 15px ui-monospace,Consolas,monospace;color:var(--brass2)}
.msg{font-weight:600}.meta{color:var(--dim);font-size:13px}
.label{font-size:12px;border-radius:6px;padding:1px 8px;background:#24345e;color:var(--teal)}
.label.head{background:var(--brass);color:#2a1f05;font-weight:700}
.change{font:13px ui-monospace,Consolas,monospace;margin:8px 0 0;border-left:3px solid var(--line);padding-left:10px;
white-space:pre-wrap;word-break:break-word}
.loc{color:var(--brass2)}.minus{color:var(--del)}.plus{color:var(--add)}
.sheet{overflow-x:auto}table{border-collapse:collapse;font:13px ui-monospace,Consolas,monospace}
th{color:var(--dim);font-weight:400;padding:4px 10px;background:var(--panel2)}
td{border:1px solid var(--line);padding:5px 10px;min-width:64px;background:#10182e}
td.f{color:var(--teal)}td.f::before{content:"\0192 ";color:var(--dim)}
td.hot{background:#3a1424;color:var(--paradox);font-weight:700;outline:2px solid var(--paradox)}
.branches{display:flex;gap:10px;flex-wrap:wrap}.branch{display:flex;gap:8px;align-items:center}
.track{width:26px;height:4px;border-radius:2px;background:var(--teal)}.branch.cur .track{background:var(--brass)}
footer{color:var(--dim);font-size:12px;text-align:center;margin-top:40px}
</style>
)";
}

void writeChanges(std::ostream& out, const Repository& repo, const Commit& commit) {
    std::vector<FileDiff> diffs;
    if (commit.hasParent()) {
        diffs = repo.diff(commit.getParentId(), commit.getId());
    }
    if (!commit.hasParent()) {
        for (const auto& [path, snapshot] : commit.getFiles()) {
            out << "<div class=\"change\"><span class=\"plus\">+ " << escape(path) << "</span> started here</div>\n";
        }
        return;
    }
    if (diffs.empty()) {
        out << "<div class=\"change meta\">No file changed.</div>\n";
    }
    for (const FileDiff& file : diffs) {
        out << "<div class=\"change\"><b>" << escape(file.path) << "</b>";
        if (file.status == FileDiff::Status::Added) {
            out << " <span class=\"plus\">(new file)</span>";
        } else if (file.status == FileDiff::Status::Removed) {
            out << " <span class=\"minus\">(removed)</span>";
        }
        for (const Change& change : file.result.changes) {
            out << "\n<span class=\"loc\">@ " << escape(change.location) << "</span>";
            if (!change.before.empty()) {
                out << "\n<span class=\"minus\">- " << escape(change.before) << "</span>";
            }
            if (!change.after.empty()) {
                out << "\n<span class=\"plus\">+ " << escape(change.after) << "</span>";
            }
        }
        out << "</div>\n";
    }
}

void writeSheet(std::ostream& out, const std::string& path, const std::string& content,
                const std::set<std::string>& hotCells) {
    const Grid<Cell> grid = CSVHandler::parse(content);
    const SheetEvaluator evaluator(grid);
    const int rows = std::min(grid.rowCount(), kMaxRows);
    const int cols = std::min(grid.colCount(), kMaxCols);
    out << "<div class=\"card\"><div class=\"msg\">" << escape(path) << "</div><div class=\"sheet\"><table><tr><th></th>";
    for (int col = 0; col < cols; ++col) {
        out << "<th>" << columnName(col) << "</th>";
    }
    out << "</tr>\n";
    for (int row = 0; row < rows; ++row) {
        out << "<tr><th>" << row + 1 << "</th>";
        for (int col = 0; col < cols; ++col) {
            const CellRef ref{row, col};
            const Cell& cell = grid.get(ref);
            std::string value;
            try {
                value = valueToString(evaluator.valueAt(ref));
            } catch (const std::exception&) {
                value = "#ERROR";
            }
            std::string cls = cell.isFormula() ? "f" : "";
            if (hotCells.count(ref.toString()) != 0) {
                cls += " hot";
            }
            out << "<td class=\"" << cls << "\" title=\"" << escape(ref.toString() + "  " + cell.getRaw()) << "\">"
                << escape(value) << "</td>";
        }
        out << "</tr>\n";
    }
    out << "</table></div>";
    if (grid.rowCount() > rows || grid.colCount() > cols) {
        out << "<div class=\"meta\">Showing the first " << rows << " rows and " << cols << " columns.</div>";
    }
    out << "<div class=\"meta\">&#402; marks a formula; hover a cell to see what it holds.</div></div>\n";
}

}  // namespace

std::string HtmlView::render(const Repository& repo) {
    std::ostringstream out;
    const std::vector<Branch> branches = repo.getBranches();
    const std::vector<Paradox> paradoxes = findParadoxes(repo);
    const std::string name = repo.getRoot().filename().string();

    out << "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Chrono: "
        << escape(name) << "</title>\n";
    writeStyle(out);
    out << "</head><body><div class=\"wrap\">\n<header><div class=\"dial\"></div><div><h1><span>Chrono</span> "
        << escape(name) << "</h1><div class=\"tag\">Every version of your work, frozen in time. "
           "Spreadsheets included, paradoxes caught.</div><div class=\"stats\">"
        << "<span class=\"chip\">on <b>"
        << escape(repo.getCurrentBranch().empty() ? "a past commit" : repo.getCurrentBranch()) << "</b></span>"
        << chip(repo.log().size(), "commit", "commits") << chip(branches.size(), "branch", "branches")
        << chip(repo.getTracked().size(), "file tracked", "files tracked")
        << chip(paradoxes.size(), "paradox", "paradoxes") << "</div></div></header>\n";

    // Paradoxes first: they are what the page is for.
    out << "<h2>&#9888; Paradox radar</h2>\n";
    if (paradoxes.empty()) {
        out << "<div class=\"card ok\">All clear. No merge has produced a value that nobody saw.</div>\n";
    }
    for (const Paradox& paradox : paradoxes) {
        const CellImpact& impact = paradox.impact;
        out << "<div class=\"card paradox\"><div class=\"big\">Paradox detected in " << escape(paradox.path) << ", cell "
            << impact.cell.toString() << "</div><div class=\"meta\">Merge commit #" << paradox.commitId << "</div><div>"
            << escape(impact.explanation) << "</div><div class=\"vals\">"
            << "<div><small>before</small>" << escape(impact.base) << "</div>"
            << "<div><small>this branch saw</small>" << escape(impact.ours) << "</div>"
            << "<div><small>the other branch saw</small>" << escape(impact.theirs) << "</div>"
            << "<div class=\"now\"><small>merged reality</small>" << escape(impact.merged) << "</div></div></div>\n";
    }

    // Each sheet as it stands at HEAD, with any paradox cell lit up.
    std::map<std::string, std::set<std::string>> hot;
    for (const Paradox& paradox : paradoxes) {
        hot[paradox.path].insert(paradox.impact.cell.toString());
    }
    if (repo.getHead() != 0) {
        bool heading = false;
        for (const auto& [path, snapshot] : repo.getCommit(repo.getHead()).getFiles()) {
            if (!Repository::isSheet(path)) {
                continue;
            }
            if (!heading) {
                out << "<h2>&#9638; Sheets right now</h2>\n";
                heading = true;
            }
            try {
                writeSheet(out, path, snapshot.getContent(), hot[path]);
            } catch (const std::exception& e) {
                out << "<div class=\"card\">" << escape(path) << ": " << escape(e.what()) << "</div>\n";
            }
        }
    }

    out << "<h2>&#9741; Branches</h2>\n<div class=\"card branches\">";
    for (const Branch& branch : branches) {
        const bool current = branch.getName() == repo.getCurrentBranch();
        out << "<div class=\"branch" << (current ? " cur" : "") << "\"><span class=\"track\"></span><b>"
            << escape(branch.getName()) << "</b> <span class=\"meta\">at #" << branch.getCommitId()
            << (current ? ", you are here" : "") << "</span></div>";
    }
    out << "</div>\n";

    out << "<h2>&#8987; Time stream</h2>\n<div class=\"stream\">\n";
    if (repo.log().size() == 0) {
        out << "<div class=\"card meta\">No commits yet. Run chrono commit -m \"first\" to start the clock.</div>\n";
    }
    for (std::size_t id = repo.log().size(); id >= 1; --id) {
        const Commit& commit = repo.getCommit(static_cast<int>(id));
        const bool merge = commit.getMergedParentId() != 0;
        const bool head = commit.getId() == repo.getHead();
        out << "<details class=\"card commit" << (merge ? " merge" : "") << (head ? " head" : "") << "\""
            << (head ? " open" : "") << "><summary><span class=\"id\">#" << commit.getId() << "</span><span class=\"msg\">"
            << escape(commit.getMessage()) << "</span>";
        if (head) {
            out << "<span class=\"label head\">HEAD</span>";
        }
        for (const Branch& branch : branches) {
            if (branch.getCommitId() == commit.getId()) {
                out << "<span class=\"label\">" << escape(branch.getName()) << "</span>";
            }
        }
        out << "<span class=\"meta\">" << escape(commit.getAuthor()) << " &middot; " << when(commit.getTimestamp());
        if (merge) {
            out << " &middot; merged #" << commit.getMergedParentId() << " into #" << commit.getParentId();
        }
        out << "</span></summary>\n";
        writeChanges(out, repo, commit);
        out << "</details>\n";
    }
    out << "</div>\n<footer>Drawn by <b>chrono view</b> at " << when(std::time(nullptr))
        << ". Nothing here changes your files; run chrono commands to do that.</footer>\n</div></body></html>\n";
    return out.str();
}
