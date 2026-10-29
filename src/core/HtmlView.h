#pragma once

#include <string>

class Repository;

// The repository as one self-contained web page, for people who would rather look
// than type: the commit timeline with each commit's changes, every branch, each
// tracked sheet drawn as a grid with its formulas worked out, and every "paradox",
// a merged sheet value that neither branch ever saw (see MergeImpactReport).
//
// Read-only: it shows the repository and never changes it.
class HtmlView {
public:
    static std::string render(const Repository& repo);
};
