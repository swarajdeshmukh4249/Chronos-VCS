#include "core/Commit.h"

#include <utility>

Commit::Commit(int id, int parentId, std::string author, std::string message, std::time_t timestamp,
               Files files)
    : id_(id),
      parentId_(parentId),
      author_(std::move(author)),
      message_(std::move(message)),
      timestamp_(timestamp),
      files_(std::move(files)) {}

const Snapshot* Commit::findFile(const std::string& path) const {
    auto it = files_.find(path);
    return it == files_.end() ? nullptr : &it->second;
}

bool Commit::hasSameFiles(const Files& files) const {
    if (files.size() != files_.size()) {
        return false;
    }
    for (const auto& [path, snapshot] : files) {
        const Snapshot* mine = findFile(path);
        if (!mine || mine->getContent() != snapshot.getContent()) {
            return false;
        }
    }
    return true;
}

std::string Commit::formattedTime() const {
    char buffer[32];
    const std::tm* local = std::localtime(&timestamp_);
    if (!local || std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", local) == 0) {
        return "unknown time";
    }
    return buffer;
}

std::string Commit::summary() const {
    return "#" + std::to_string(id_) + "  " + formattedTime() + "  " + author_ + "  " + message_;
}
