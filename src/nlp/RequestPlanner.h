#pragma once

#include <string>
#include <vector>

#include "nlp/DetailExtractor.h"
#include "nlp/IntentParser.h"
#include "nlp/RequestContext.h"

// What should happen about a plain-English request.
struct RequestPlan {
    enum class Outcome {
        Run,         // understood, and it can't lose anything: just do it
        Confirm,     // understood, but it overwrites or rewinds work: ask first
        DidYouMean,  // the words have more than one reading, or nearly name something real
        Unclear,     // nothing usable came out
    };

    Outcome outcome = Outcome::Unclear;

    // The command-line words the request comes down to, such as {"revert", "3"}.
    // Set only for Run and Confirm. Going back out through the ordinary command
    // line means this layer needs no commands of its own: it builds the same
    // Command objects through CommandFactory that a typed command would.
    std::vector<std::string> args;
    // The one-line summary of what is about to happen.
    std::string action;
    // How the request was read, where that is worth saying: which commit a time
    // phrase landed on, or that nothing was saved during it.
    std::vector<std::string> notes;
    // The yes/no question to put first. Set only for Confirm.
    std::string question;
    // The readings on offer, each a whole sentence. Set only for DidYouMean.
    std::vector<std::string> options;
    // Why nothing can be done, each a whole sentence. Set only for Unclear.
    std::vector<std::string> problems;

    bool understood() const { return outcome == Outcome::Run || outcome == Outcome::Confirm; }
};

// Works out what to do about a plain-English request: which command it comes
// down to, whether to ask before doing it, and what to offer back when the
// words could mean more than one thing.
class RequestPlanner {
public:
    // parser must outlive the planner. Taking it as an interface is what lets
    // an AI-backed parser (#23) replace the rule-based one here.
    explicit RequestPlanner(const IntentParser& parser) : parser_(parser) {}

    RequestPlan plan(const std::string& request, const RequestContext& context) const;

    // True for the commands that overwrite the files in the folder or rewind
    // what is saved. These are the ones a plain-English request must confirm,
    // because the user never typed the command name and so never saw it coming.
    static bool canLoseWork(IntentKind kind);

    // What a command does, in a few plain words, for offering choices back.
    static std::string plainDescription(IntentKind kind);

private:
    const IntentParser& parser_;
};
