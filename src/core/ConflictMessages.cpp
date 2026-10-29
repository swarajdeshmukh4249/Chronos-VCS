#include "core/ConflictMessages.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

// How far the content of one side is pushed in when it needs lines of its own.
const char* const kBlockIndent = "      ";

std::string branchLabel(const std::string& name) {
    return name.empty() ? "this commit" : name;
}

// "C2" and "AB13" are cell references. "row 4", "line 3" and "lines 2-3" are
// not, so a sheet's row conflicts keep reading as rows.
bool looksLikeCellRef(const std::string& location) {
    std::size_t letters = 0;
    while (letters < location.size() && std::isalpha(static_cast<unsigned char>(location[letters])) != 0) {
        ++letters;
    }
    if (letters == 0 || letters == location.size()) {
        return false;
    }
    return std::all_of(location.begin() + static_cast<std::ptrdiff_t>(letters), location.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

// A bare cell reference means nothing on its own, so say what it is.
std::string place(const std::string& path, const std::string& location) {
    if (Repository::isSheet(path) && looksLikeCellRef(location)) {
        return "cell " + location;
    }
    return location;
}

std::vector<std::string> contentLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

// One side's content. A single line sits next to its label; several lines go
// underneath it, indented, so whole lines of text stay readable.
void appendSide(std::vector<std::string>& details, const std::string& label, const std::string& content) {
    const std::vector<std::string> lines = contentLines(content);
    if (lines.empty()) {
        details.push_back(label + ": nothing");
        return;
    }
    if (lines.size() == 1) {
        details.push_back(label + ": " + lines.front());
        return;
    }
    details.push_back(label + ":");
    for (const std::string& line : lines) {
        details.push_back(kBlockIndent + line);
    }
}

}  // namespace

ConflictMessage ConflictMessages::explain(const std::string& path, const Conflict& conflict, const std::string& ours,
                                          const std::string& theirs) {
    ConflictMessage message;
    message.headline = path + ", " + place(path, conflict.location);
    if (!conflict.explanation.empty()) {
        message.details.push_back(conflict.explanation);
    }
    appendSide(message.details, "on " + branchLabel(ours), conflict.ours);
    appendSide(message.details, "on " + branchLabel(theirs), conflict.theirs);
    // Only worth showing when there was something there to begin with: a pure
    // addition has no "before".
    if (!conflict.base.empty()) {
        appendSide(message.details, "before", conflict.base);
    }
    message.advice = Repository::isSheet(path)
                         ? "Both values are in the cell now, between <<<<<<< and >>>>>>>. Replace the whole cell "
                           "with the one you want."
                         : "Keep the lines you want, then delete the <<<<<<<, ======= and >>>>>>> lines.";
    return message;
}

std::vector<std::string> ConflictMessages::report(const MergeOutcome& outcome, const std::string& ours,
                                                  const std::string& theirs) {
    std::vector<std::string> lines;
    const std::size_t count = outcome.conflicts.size();
    lines.push_back("Merge stopped: " + std::to_string(count) + (count == 1 ? " conflict" : " conflicts") +
                    " to fix.");
    // The explanations below are written by the merge itself, which only knows
    // the two sides as "this branch" and "the other branch", so say which is which.
    lines.push_back("Below, \"this branch\" is " + branchLabel(ours) + " and \"the other branch\" is " +
                    branchLabel(theirs) + ".");

    for (const MergeOutcome::FileConflict& file : outcome.conflicts) {
        const ConflictMessage message = explain(file.path, file.conflict, ours, theirs);
        lines.push_back("");
        lines.push_back(message.headline);
        for (const std::string& detail : message.details) {
            lines.push_back("  " + detail);
        }
        lines.push_back("  " + message.advice);
    }

    lines.push_back("");
    lines.push_back("When every file is fixed:  chrono commit -m \"<message>\"");
    lines.push_back("To cancel the merge:       chrono merge --abort   (or chrono undo)");
    return lines;
}
