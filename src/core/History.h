#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// An append-only timeline of T, oldest first. Entries are numbered from 1,
// matching what the user sees ("revert to commit 1").
template <typename T>
class History {
public:
    void append(T item) { items_.push_back(std::move(item)); }

    std::size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    bool contains(std::size_t number) const { return number >= 1 && number <= items_.size(); }

    // Throws std::out_of_range unless 1 <= number <= size().
    const T& at(std::size_t number) const {
        if (!contains(number)) {
            throw std::out_of_range("History has no entry " + std::to_string(number));
        }
        return items_[number - 1];
    }

    // Throws std::out_of_range when empty.
    const T& latest() const { return at(items_.size()); }

    typename std::vector<T>::const_iterator begin() const { return items_.begin(); }
    typename std::vector<T>::const_iterator end() const { return items_.end(); }

private:
    std::vector<T> items_;
};
