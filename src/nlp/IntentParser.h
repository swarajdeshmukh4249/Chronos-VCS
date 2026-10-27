#pragma once

#include <string>
#include <vector>

#include "nlp/Intent.h"

// Turns a plain-English request into an Intent. RuleBasedParser does this with
// word lists; an AI-backed parser (#23) can replace it without other changes.
class IntentParser {
public:
    virtual ~IntentParser() = default;

    // Every intent the request could mean, most likely first. Intents with a
    // score of 0 are left out, so an empty list means nothing matched.
    virtual std::vector<Intent> rank(const std::string& request) const = 0;

    // The most likely intent, or Unknown when nothing matched or the top two
    // are equally likely, so a guess never picks a command on a coin flip.
    // An Unknown intent still carries the request's tokens.
    Intent parse(const std::string& request) const;
};
