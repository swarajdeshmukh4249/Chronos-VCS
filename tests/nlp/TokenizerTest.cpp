#include "TestHarness.h"

#include <string>
#include <vector>

#include "nlp/Tokenizer.h"

namespace {

std::vector<std::string> words(const std::string& sentence) {
    std::vector<std::string> result;
    for (const Token& token : Tokenizer::tokenize(sentence)) {
        result.push_back(token.word);
    }
    return result;
}

}  // namespace

// Issue #16
TEST(tokenizerLowerCasesWordsAndDropsPunctuation) {
    const std::vector<Token> tokens = Tokenizer::tokenize("Go back to Monday's version!");
    CHECK(words("Go back to Monday's version!") ==
          (std::vector<std::string>{"go", "back", "to", "monday", "version"}));
    CHECK(tokens[3].text == "Monday's");
    CHECK(!tokens[3].quoted);
}

TEST(tokenizerKeepsFileNamesAndCommitNumbers) {
    const std::vector<Token> tokens = Tokenizer::tokenize("add Budget.csv, then (#12).");
    CHECK(tokens.size() == 4);
    CHECK(tokens[1].text == "Budget.csv");
    CHECK(tokens[1].word == "budget.csv");
    CHECK(tokens[3].text == "#12");
}

TEST(tokenizerKeepsQuotedTextTogether) {
    std::vector<Token> tokens = Tokenizer::tokenize("save it as \"Fixed  Totals\" now");
    CHECK(tokens.size() == 5);
    CHECK(tokens[3].text == "Fixed  Totals");
    CHECK(tokens[3].word == "fixed  totals");
    CHECK(tokens[3].quoted);
    CHECK(!tokens[4].quoted);

    tokens = Tokenizer::tokenize("commit 'tidy up' please");
    CHECK(tokens.size() == 3);
    CHECK(tokens[1].text == "tidy up");
    CHECK(tokens[1].quoted);
}

TEST(tokenizerTreatsApostrophesAsPartOfWords) {
    CHECK(words("let's undo Monday's change") == (std::vector<std::string>{"let", "undo", "monday", "change"}));
    CHECK(words("it's 'quoted'") == (std::vector<std::string>{"it", "quoted"}));
}

TEST(tokenizerUnderstandsCurlyQuotes) {
    const std::vector<Token> tokens = Tokenizer::tokenize("save as \xE2\x80\x9C" "final\xE2\x80\x9D for Monday\xE2\x80\x99s demo");
    CHECK(tokens.size() == 6);
    CHECK(tokens[2].text == "final");
    CHECK(tokens[2].quoted);
    CHECK(tokens[4].word == "monday");
}

TEST(tokenizerHandlesEmptyAndUnclosedInput) {
    CHECK(Tokenizer::tokenize("").empty());
    CHECK(Tokenizer::tokenize("   ?! ...").empty());
    CHECK(Tokenizer::tokenize("\"\"").empty());
    CHECK(words("commit \"unfinished") == (std::vector<std::string>{"commit", "unfinished"}));
}
