#include "index.h"

#include <numeric>

#include "tokenizer.h"

namespace {
const Postings kEmpty;
}

int Index::addLine(const std::string& line) {
    lines_.push_back(line);
    int id = static_cast<int>(lines_.size()) - 1;
    for (const auto& token : tokenize(line)) {
        Postings& p = postings_[token];
        // ids only grow, so lists stay sorted; skip duplicates within one line
        if (p.empty() || p.back() != id) p.push_back(id);
    }
    return id;
}

const Postings& Index::lookup(const std::string& term) const {
    auto it = postings_.find(term);
    return it == postings_.end() ? kEmpty : it->second;
}

Postings Index::allIds() const {
    Postings all(lines_.size());
    std::iota(all.begin(), all.end(), 0);
    return all;
}