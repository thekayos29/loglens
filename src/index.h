#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// Sorted list of line ids containing a term.
using Postings = std::vector<int>;

// In-memory inverted index: term -> posting list.
// Not thread-safe by itself; the server wraps it in a shared_mutex.
class Index {
public:
    int addLine(const std::string& line);                   // returns the new line id
    const Postings& lookup(const std::string& term) const;  // empty if unknown
    const std::string& line(int id) const { return lines_[static_cast<size_t>(id)]; }
    int size() const { return static_cast<int>(lines_.size()); }
    Postings allIds() const;

private:
    std::vector<std::string> lines_;
    std::unordered_map<std::string, Postings> postings_;
};