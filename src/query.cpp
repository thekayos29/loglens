#include "query.h"

#include <cctype>
#include <vector>

#include "tokenizer.h"

Postings intersect(const Postings& a, const Postings& b) {
    Postings out;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { out.push_back(a[i]); ++i; ++j; }
        else if (a[i] < b[j]) ++i;
        else ++j;
    }
    return out;
}

Postings unite(const Postings& a, const Postings& b) {
    Postings out;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { out.push_back(a[i]); ++i; ++j; }
        else if (a[i] < b[j]) out.push_back(a[i++]);
        else out.push_back(b[j++]);
    }
    while (i < a.size()) out.push_back(a[i++]);
    while (j < b.size()) out.push_back(b[j++]);
    return out;
}

Postings difference(const Postings& a, const Postings& b) {
    Postings out;
    size_t i = 0, j = 0;
    while (i < a.size()) {
        if (j >= b.size() || a[i] < b[j]) out.push_back(a[i++]);
        else if (a[i] == b[j]) { ++i; ++j; }
        else ++j;
    }
    return out;
}

namespace {

// Lexer: split into "(", ")" and words.
std::vector<std::string> lex(const std::string& q) {
    std::vector<std::string> toks;
    std::string cur;
    auto flush = [&] { if (!cur.empty()) { toks.push_back(cur); cur.clear(); } };
    for (char c : q) {
        if (c == '(' || c == ')') { flush(); toks.push_back(std::string(1, c)); }
        else if (std::isspace(static_cast<unsigned char>(c))) flush();
        else cur += c;
    }
    flush();
    return toks;
}

// Parser that evaluates as it goes (no explicit tree needed).
class Parser {
public:
    Parser(const Index& idx, std::vector<std::string> toks)
        : idx_(idx), toks_(std::move(toks)) {}

    Postings parse() {
        if (toks_.empty()) throw QueryError("empty query");
        Postings r = parseOr();
        if (pos_ != toks_.size()) throw QueryError("unexpected '" + toks_[pos_] + "'");
        return r;
    }

private:
    const Index& idx_;
    std::vector<std::string> toks_;
    size_t pos_ = 0;

    bool peekIs(const char* s) const { return pos_ < toks_.size() && toks_[pos_] == s; }
    bool startsPrimary() const {
        if (pos_ >= toks_.size()) return false;
        const std::string& t = toks_[pos_];
        return t != "AND" && t != "OR" && t != ")";
    }

    Postings parseOr() {
        Postings left = parseAnd();
        while (peekIs("OR")) { ++pos_; left = unite(left, parseAnd()); }
        return left;
    }

    Postings parseAnd() {
        Postings left = parseNot();
        for (;;) {
            if (peekIs("AND")) { ++pos_; left = intersect(left, parseNot()); }
            else if (startsPrimary()) left = intersect(left, parseNot());  // implicit AND
            else break;
        }
        return left;
    }

    Postings parseNot() {
        if (peekIs("NOT")) { ++pos_; return difference(idx_.allIds(), parseNot()); }
        return parsePrimary();
    }

    Postings parsePrimary() {
        if (pos_ >= toks_.size()) throw QueryError("unexpected end of query");
        std::string t = toks_[pos_++];
        if (t == "(") {
            Postings r = parseOr();
            if (!peekIs(")")) throw QueryError("missing ')'");
            ++pos_;
            return r;
        }
        if (t == ")" || t == "AND" || t == "OR") throw QueryError("unexpected '" + t + "'");
        return term(t);
    }

    // A word like "user=42" tokenizes to {user, 42}; require all of them.
    Postings term(const std::string& word) const {
        auto parts = tokenize(word);
        if (parts.empty()) throw QueryError("no searchable characters in '" + word + "'");
        Postings r = idx_.lookup(parts[0]);
        for (size_t i = 1; i < parts.size(); ++i) r = intersect(r, idx_.lookup(parts[i]));
        return r;
    }
};

}  // namespace

Postings runQuery(const Index& index, const std::string& query) {
    return Parser(index, lex(query)).parse();
}