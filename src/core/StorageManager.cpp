#include "core/StorageManager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include "common/Exceptions.h"
#include "core/ContentHash.h"

namespace fs = std::filesystem;

namespace {

// A commit file looks like this. The message length is in bytes, so a message
// can hold any text, including newlines. Each file names the object holding its
// content, and "check" is the hash of everything after the first line.
//   check 3b0f6a51c2d47e98
//   id 3
//   parent 2
//   author Swaraj
//   time 1793015606
//   message 12
//   fixed totals
//   file af63dc4c8601ec8c notes.txt

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw CorruptObjectException("Could not read " + path.string());
    }
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

// Writes a temporary file and renames it, so a crash never leaves half a file behind.
void writeFile(const fs::path& path, const std::string& content) {
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << content;
        if (!out) {
            throw ChronoException("Could not write " + path.string());
        }
    }
    std::error_code error;
    fs::rename(temp, path, error);
    if (error) {
        throw ChronoException("Could not write " + path.string() + ": " + error.message());
    }
}

class CommitReader {
public:
    CommitReader(const std::string& text, std::string name) : text_(text), name_(std::move(name)) {}

    bool atEnd() const { return pos_ == text_.size(); }
    std::size_t position() const { return pos_; }

    // The rest of a "key value" line, after checking the key.
    std::string field(const std::string& key) {
        const std::size_t end = text_.find('\n', pos_);
        if (end == std::string::npos) {
            fail("expected \"" + key + "\" but the file ended");
        }
        const std::string line = text_.substr(pos_, end - pos_);
        if (line.rfind(key + " ", 0) != 0) {
            fail("expected \"" + key + "\" but found \"" + line + "\"");
        }
        pos_ = end + 1;
        return line.substr(key.size() + 1);
    }

    // length bytes followed by '\n'.
    std::string block(std::size_t length) {
        if (length >= text_.size() - pos_ || text_[pos_ + length] != '\n') {
            fail("a block is shorter than its stated length");
        }
        std::string result = text_.substr(pos_, length);
        pos_ += length + 1;
        return result;
    }

    long long number(const std::string& text) {
        if (text.empty() || text.size() > 18) {
            fail("\"" + text + "\" is not a number");
        }
        long long result = 0;
        for (char c : text) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                fail("\"" + text + "\" is not a number");
            }
            result = result * 10 + (c - '0');
        }
        return result;
    }

    // A number that must fit in an int, such as a commit id.
    int smallNumber(const std::string& text) {
        const long long result = number(text);
        if (result > std::numeric_limits<int>::max()) {
            fail("\"" + text + "\" is too large");
        }
        return static_cast<int>(result);
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw CorruptObjectException(name_ + " is damaged: " + message);
    }

private:
    const std::string& text_;
    std::string name_;
    std::size_t pos_ = 0;
};

std::string formatCommit(const Commit& commit) {
    std::string author = commit.getAuthor();
    for (char& c : author) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    std::ostringstream body;
    body << "id " << commit.getId() << "\n"
         << "parent " << commit.getParentId() << "\n"
         << "author " << author << "\n"
         << "time " << static_cast<long long>(commit.getTimestamp()) << "\n"
         << "message " << commit.getMessage().size() << "\n"
         << commit.getMessage() << "\n";
    for (const auto& [path, snapshot] : commit.getFiles()) {
        body << "file " << contentHash(snapshot.getContent()) << " " << path << "\n";
    }
    return "check " + contentHash(body.str()) + "\n" + body.str();
}

// What a commit file says, before the file contents are read from objects/.
struct CommitRecord {
    int id = 0;
    int parentId = 0;
    std::string author;
    std::time_t timestamp = 0;
    std::string message;
    std::vector<std::pair<std::string, std::string>> files;  // path, content hash
    bool checksumMatches = false;
};

CommitRecord parseCommitRecord(const std::string& text, const std::string& name) {
    CommitReader reader(text, name);
    CommitRecord record;
    const std::string checksum = reader.field("check");
    if (!isContentHash(checksum)) {
        reader.fail("\"" + checksum + "\" is not a checksum");
    }
    record.checksumMatches = contentHash(text.substr(reader.position())) == checksum;
    record.id = reader.smallNumber(reader.field("id"));
    record.parentId = reader.smallNumber(reader.field("parent"));
    record.author = reader.field("author");
    record.timestamp = static_cast<std::time_t>(reader.number(reader.field("time")));
    record.message = reader.block(static_cast<std::size_t>(reader.number(reader.field("message"))));

    while (!reader.atEnd()) {
        const std::string entry = reader.field("file");
        const std::size_t space = entry.find(' ');
        if (space == std::string::npos || space + 1 == entry.size()) {
            reader.fail("a file entry has no path");
        }
        const std::string hash = entry.substr(0, space);
        if (!isContentHash(hash)) {
            reader.fail("\"" + hash + "\" is not a content hash");
        }
        record.files.emplace_back(entry.substr(space + 1), hash);
    }
    return record;
}

std::string trimLineEnd(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

std::string objectName(const std::string& hash) {
    return std::string(StorageManager::kFolderName) + "/objects/" + hash;
}

// The id in a commit file name such as "12.txt", or 0 for any other file.
int commitIdFromFileName(const fs::path& path) {
    const std::string stem = path.stem().string();
    if (path.extension() != ".txt" || stem.empty() || stem.size() > 9 || stem[0] == '0') {
        return 0;
    }
    int id = 0;
    for (char c : stem) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return 0;
        }
        id = id * 10 + (c - '0');
    }
    return id;
}

}  // namespace

StorageManager::StorageManager(const fs::path& root) : folder_(root / kFolderName) {}

fs::path StorageManager::findRoot(const fs::path& start) {
    for (fs::path dir = fs::weakly_canonical(fs::absolute(start));; dir = dir.parent_path()) {
        if (fs::is_directory(dir / kFolderName)) {
            return dir;
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }
    throw RepositoryNotFoundException("Not inside a Chrono repository. Run \"chrono init\" first.");
}

bool StorageManager::isInitialized() const {
    return fs::is_directory(folder_);
}

void StorageManager::initialize() const {
    if (fs::exists(folder_)) {
        throw RepositoryExistsException("A Chrono repository already exists here");
    }
    fs::create_directories(folder_ / "commits");
    fs::create_directories(folder_ / "objects");
    saveTracked({});
    saveBranch(Branch("main", 0));
    saveHead(HeadRef{"main", 0});
}

void StorageManager::saveCommit(const Commit& commit) const {
    std::error_code ignored;
    fs::create_directories(folder_ / "objects", ignored);
    for (const auto& [path, snapshot] : commit.getFiles()) {
        // Identical content has the same hash, so each version of a file is stored once.
        const fs::path object = folder_ / "objects" / contentHash(snapshot.getContent());
        if (!fs::exists(object)) {
            writeFile(object, snapshot.getContent());
        }
    }
    writeFile(commitPath(commit.getId()), formatCommit(commit));
}

History<Commit> StorageManager::loadCommits() const {
    History<Commit> history;
    const int highest = highestCommitId();
    for (int id = 1; id <= highest; ++id) {
        const std::string name = commitName(id);
        if (!fs::is_regular_file(commitPath(id))) {
            throw CorruptObjectException(name + " is missing");
        }
        CommitRecord record = parseCommitRecord(readFile(commitPath(id)), name);
        if (record.id != id) {
            throw CorruptObjectException(name + " is damaged: it says it is commit " + std::to_string(record.id));
        }
        Commit::Files files;
        for (const auto& [path, hash] : record.files) {
            const fs::path object = folder_ / "objects" / hash;
            if (!fs::is_regular_file(object)) {
                throw CorruptObjectException(name + " is damaged: the saved copy of " + path + " (" +
                                             objectName(hash) + ") is missing");
            }
            files.emplace(path, Snapshot(path, readFile(object)));
        }
        history.append(Commit(record.id, record.parentId, std::move(record.author), std::move(record.message),
                              record.timestamp, std::move(files)));
    }
    return history;
}

IntegrityReport StorageManager::verify() const {
    IntegrityReport report;
    std::map<std::string, bool> intactObjects;
    const int highest = highestCommitId();
    for (int id = 1; id <= highest; ++id) {
        const std::string file = std::string(kFolderName) + "/commits/" + std::to_string(id) + ".txt";
        const std::string commit = "commit #" + std::to_string(id);
        if (!fs::is_regular_file(commitPath(id))) {
            report.problems.push_back({file, "is missing"});
            continue;
        }
        CommitRecord record;
        try {
            record = parseCommitRecord(readFile(commitPath(id)), commitName(id));
        } catch (const CorruptObjectException& e) {
            // The message starts with the commit's name; the report already shows the file.
            const std::string what = e.what();
            const std::size_t damaged = what.find("is damaged");
            report.problems.push_back({file, damaged == std::string::npos ? what : what.substr(damaged)});
            continue;
        }
        ++report.commits;
        if (!record.checksumMatches) {
            report.problems.push_back({file, "was edited after it was saved (" + commit + ")"});
        }

        for (const auto& [path, hash] : record.files) {
            auto known = intactObjects.find(hash);
            if (known != intactObjects.end()) {
                continue;
            }
            const fs::path object = folder_ / "objects" / hash;
            bool intact = fs::is_regular_file(object);
            if (!intact) {
                report.problems.push_back({objectName(hash), "is missing (" + path + " in " + commit + ")"});
            } else if (contentHash(readFile(object)) != hash) {
                intact = false;
                report.problems.push_back({objectName(hash), "was changed (" + path + " in " + commit + ")"});
            }
            intactObjects.emplace(hash, intact);
        }
    }
    report.objects = static_cast<int>(intactObjects.size());
    return report;
}

int StorageManager::highestCommitId() const {
    // Only names like "12.txt" are commits, so a stray backup or copied-in file is ignored.
    int highest = 0;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder_ / "commits", error)) {
        highest = std::max(highest, commitIdFromFileName(entry.path()));
    }
    if (error) {
        throw CorruptObjectException("Could not list " + (folder_ / "commits").string());
    }
    return highest;
}

fs::path StorageManager::commitPath(int id) const {
    return folder_ / "commits" / (std::to_string(id) + ".txt");
}

std::string StorageManager::commitName(int id) {
    return "Commit " + std::to_string(id) + " (" + kFolderName + "/commits/" + std::to_string(id) + ".txt)";
}

void StorageManager::saveTracked(const std::set<std::string>& paths) const {
    std::string content;
    for (const std::string& path : paths) {
        content += path + "\n";
    }
    writeFile(folder_ / "tracked", content);
}

void StorageManager::saveHead(const HeadRef& head) const {
    writeFile(folder_ / "HEAD", head.branch.empty() ? std::to_string(head.commitId) + "\n" : "ref: " + head.branch + "\n");
}

HeadRef StorageManager::loadHead() const {
    const std::string name = std::string(kFolderName) + "/HEAD";
    const std::string text = trimLineEnd(readFile(folder_ / "HEAD"));
    CommitReader reader(text, name);
    HeadRef head;
    if (text.rfind("ref: ", 0) == 0) {
        head.branch = text.substr(5);
        if (!Branch::isValidName(head.branch)) {
            reader.fail("\"" + head.branch + "\" is not a branch name");
        }
    } else {
        head.commitId = reader.smallNumber(text);
    }
    return head;
}

void StorageManager::saveBranch(const Branch& branch) const {
    std::error_code ignored;
    fs::create_directories(folder_ / "branches", ignored);
    writeFile(folder_ / "branches" / branch.getName(), std::to_string(branch.getCommitId()) + "\n");
}

std::vector<Branch> StorageManager::loadBranches() const {
    std::vector<Branch> branches;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder_ / "branches", error)) {
        const std::string name = entry.path().filename().string();
        // Skips files that can't be branches, such as .DS_Store or editor backups.
        if (!entry.is_regular_file() || !Branch::isValidName(name)) {
            continue;
        }
        const std::string text = trimLineEnd(readFile(entry.path()));
        CommitReader reader(text, std::string(kFolderName) + "/branches/" + name);
        branches.emplace_back(name, reader.smallNumber(text));
    }
    if (error) {
        throw CorruptObjectException("Could not list " + (folder_ / "branches").string());
    }
    std::sort(branches.begin(), branches.end(),
              [](const Branch& a, const Branch& b) { return a.getName() < b.getName(); });
    return branches;
}

std::set<std::string> StorageManager::loadTracked() const {
    std::istringstream in(readFile(folder_ / "tracked"));
    std::set<std::string> paths;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) {
            paths.insert(line);
        }
    }
    return paths;
}
