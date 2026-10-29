#pragma once

#include <string>
#include <vector>

// One word of a plain-English request.
//
// text keeps what the user typed, minus surrounding punctuation, so a file or
// branch name keeps its case ("Budget.csv"). word is the lower-case form the
// matchers compare against, with a trailing "'s" dropped ("Monday's" -> "monday").
// Anything in double quotes, or in single quotes around whole words, is one
// quoted token, such as a commit message; matchers ignore quoted tokens.
struct Token {
    std::string text;
    std::string word;
    bool quoted = false;
};

class Tokenizer {
public:
    // "Save it as \"fixed totals\"!" -> save, it, as, "fixed totals" (quoted).
    // Curly quotes and apostrophes count the same as straight ones.
    static std::vector<Token> tokenize(const std::string& sentence);
};
