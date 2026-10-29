#pragma once

#include <memory>
#include <string>
#include <vector>

#include "nlp/IntentMatcher.h"
#include "nlp/IntentParser.h"

// Asks one IntentMatcher per command how well the request fits, and ranks the answers.
class RuleBasedParser : public IntentParser {
public:
    RuleBasedParser();
    std::vector<Intent> rank(const std::string& request) const override;

private:
    std::vector<std::unique_ptr<IntentMatcher>> matchers_;
};
