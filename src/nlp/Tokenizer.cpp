#include "nlp/Tokenizer.h"

#include <cctype>
#include <cstddef>

namespace {

// Phones and word processors type curly quotes; treat them as straight ones.
std::string straightenQuotes(const std::string& sentence) {
    std::string result;
    for (std::size_t i = 0; i < sentence.size(); ++i) {
        if (sentence.compare(i, 2, "\xE2\x80") == 0 && i + 2 < sentence.size()) {
            const char last = sentence[i + 2];
            if (last == '\x9C' || last == '\x9D') {
                result += '"';
                i += 2;
                continue;
            }
            if (last == '\x98' || last == '\x99') {
                result += '\'';
                i += 2;
                continue;
            }
        }
        result += sentence[i];
    }
    return result;
}

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool isLetterOrDigit(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

std::string lowerCase(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

// Strips the punctuation around a word but keeps what's inside, such as the
// dot in "budget.csv" or the '#' in "#12".
std::string trimPunctuation(const std::string& raw) {
    const char* const punctuation = ".,!?;:()[]{}\"'";
    const std::size_t first = raw.find_first_not_of(punctuation);
    if (first == std::string::npos) {
        return "";
    }
    return raw.substr(first, raw.find_last_not_of(punctuation) - first + 1);
}

// A single quote opens a quotation only if a matching one closes a word later,
// so the apostrophes in "let's" or "Monday's" don't swallow the sentence.
std::size_t closingSingleQuote(const std::string& sentence, std::size_t open) {
    for (std::size_t i = open + 1; i < sentence.size(); ++i) {
        if (sentence[i] == '\'' && (i + 1 == sentence.size() || !isLetterOrDigit(sentence[i + 1]))) {
            return i;
        }
    }
    return std::string::npos;
}

}  // namespace

std::vector<Token> Tokenizer::tokenize(const std::string& sentence) {
    const std::string text = straightenQuotes(sentence);
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        if (isSpace(text[i])) {
            ++i;
            continue;
        }
        const char c = text[i];
        const std::size_t close = c == '"'    ? text.find('"', i + 1)
                                  : c == '\'' ? closingSingleQuote(text, i)
                                              : std::string::npos;
        if (close != std::string::npos) {
            const std::string inside = text.substr(i + 1, close - i - 1);
            if (!inside.empty()) {
                tokens.push_back({inside, lowerCase(inside), true});
            }
            i = close + 1;
            continue;
        }

        const std::size_t start = i;
        while (i < text.size() && !isSpace(text[i])) {
            ++i;
        }
        const std::string word = trimPunctuation(text.substr(start, i - start));
        if (word.empty()) {
            continue;
        }
        std::string lower = lowerCase(word);
        if (lower.size() > 2 && lower.compare(lower.size() - 2, 2, "'s") == 0) {
            lower.resize(lower.size() - 2);
        }
        tokens.push_back({word, lower, false});
    }
    return tokens;
}
