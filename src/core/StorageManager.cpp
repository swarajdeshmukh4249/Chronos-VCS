#include "core/StorageManager.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "common/Exceptions.h"

namespace fs = std::filesystem;

namespace {

// A commit file looks like this. Lengths are in bytes, so messages and file
// contents can hold any text, including newlines. Each block ends with '\n'.
//   id 3
//   parent 2
//   author Swaraj
//   time 1793015606
//   message 12
//   fixed totals
//   file 5 notes.txt
//   hello

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
    std::ostringstream out;
    out << "id " << commit.getId() << "\n"
        << "parent " << commit.getParentId() << "\n"
        << "author " << author << "\n"
        << "time " << static_cast<long long>(commit.getTimestamp()) << "\n"
        << "message " << commit.getMessage().size() << "\n"
        << commit.getMessage() << "\n";
    for (const auto& [path, snapshot] : commit.getFiles()) {
        out << "file " << snapshot.getContent().size() << " " << path << "\n" << snapshot.getContent() << "\n";
    }
    return out.str();
}

Commit parseCommit(const std::string& text, const std::string& name) {
    CommitReader reader(text, name);
    const int id = static_cast<int>(reader.number(reader.field("id")));
    const int parentId = static_cast<int>(reader.number(reader.field("parent")));
    std::string author = reader.field("author");
    const auto timestamp = static_cast<std::time_t>(reader.number(reader.field("time")));
    std::string message = reader.block(static_cast<std::size_t>(reader.number(reader.field("message"))));

    Commit::Files files;
    while (!reader.atEnd()) {
        const std::string header = reader.field("file");
        const std::size_t space = header.find(' ');
        if (space == std::string::npos || space + 1 == header.size()) {
            reader.fail("a file entry has no path");
        }
        const auto length = static_cast<std::size_t>(reader.number(header.substr(0, space)));
        std::string path = header.substr(space + 1);
        files.emplace(path, Snapshot(path, reader.block(length)));
    }
    return Commit(id, parentId, std::move(author), std::move(message), timestamp, std::move(files));
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
    saveTracked({});
}

void StorageManager::saveCommit(const Commit& commit) const {
    writeFile(folder_ / "commits" / (std::to_string(commit.getId()) + ".txt"), formatCommit(commit));
}

History<Commit> StorageManager::loadCommits() const {
    int count = 0;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder_ / "commits", error)) {
        if (entry.path().extension() == ".txt") {
            ++count;
        }
    }
    if (error) {
        throw CorruptObjectException("Could not list " + (folder_ / "commits").string());
    }

    History<Commit> history;
    for (int id = 1; id <= count; ++id) {
        const std::string name = "commit " + std::to_string(id);
        const fs::path path = folder_ / "commits" / (std::to_string(id) + ".txt");
        if (!fs::is_regular_file(path)) {
            throw CorruptObjectException(name + " is missing");
        }
        Commit commit = parseCommit(readFile(path), name);
        if (commit.getId() != id) {
            throw CorruptObjectException(name + " is damaged: it says it is commit " +
                                         std::to_string(commit.getId()));
        }
        history.append(std::move(commit));
    }
    return history;
}

void StorageManager::saveTracked(const std::set<std::string>& paths) const {
    std::string content;
    for (const std::string& path : paths) {
        content += path + "\n";
    }
    writeFile(folder_ / "tracked", content);
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
