// Dependency-free tests. Run: ctest --test-dir build   (or ./build/loglens_tests)
#include <iostream>

#include "index.h"
#include "query.h"
#include "tokenizer.h"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond << "\n";     \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static bool throws(const Index& idx, const std::string& q) {
    try { runQuery(idx, q); } catch (const QueryError&) { return true; }
    return false;
}

int main() {
    // tokenizer
    CHECK((tokenize("ERROR payment failed: timeout") ==
           std::vector<std::string>{"error", "payment", "failed", "timeout"}));
    CHECK(tokenize("").empty());
    CHECK(tokenize("--- !!!").empty());
    CHECK((tokenize("user=42") == std::vector<std::string>{"user", "42"}));

    // set operations
    CHECK((intersect({1, 3, 5}, {3, 4, 5}) == Postings{3, 5}));
    CHECK(intersect({}, {1, 2}).empty());
    CHECK((unite({1, 3}, {2, 3, 4}) == Postings{1, 2, 3, 4}));
    CHECK((difference({1, 2, 3, 4}, {2, 4}) == Postings{1, 3}));
    CHECK((difference({1, 2}, {}) == Postings{1, 2}));

    // queries
    Index idx;
    idx.addLine("ERROR payment failed timeout");   // 0
    idx.addLine("INFO user=42 logged in");         // 1
    idx.addLine("ERROR payment gateway refused");  // 2
    idx.addLine("DEBUG payment ok");               // 3
    idx.addLine("WARN timeout refused retrying");  // 4

    CHECK((runQuery(idx, "timeout") == Postings{0, 4}));
    CHECK((runQuery(idx, "TIMEOUT") == Postings{0, 4}));  // case-insensitive terms
    CHECK((runQuery(idx, "payment AND timeout") == Postings{0}));
    CHECK((runQuery(idx, "payment timeout") == Postings{0}));  // implicit AND
    CHECK((runQuery(idx, "timeout OR refused") == Postings{0, 2, 4}));
    CHECK((runQuery(idx, "error AND NOT timeout") == Postings{2}));
    CHECK((runQuery(idx, "NOT payment") == Postings{1, 4}));
    CHECK((runQuery(idx, "payment AND (timeout OR refused)") == Postings{0, 2}));
    CHECK((runQuery(idx, "user=42") == Postings{1}));
    CHECK(runQuery(idx, "doesnotexist").empty());
    // precedence: AND binds tighter than OR -> info OR (error AND refused)
    CHECK((runQuery(idx, "info OR error AND refused") == Postings{1, 2}));
    // NOT binds tighter than AND -> (NOT debug) AND payment
    CHECK((runQuery(idx, "NOT debug AND payment") == Postings{0, 2}));

    // malformed queries
    CHECK(throws(idx, ""));
    CHECK(throws(idx, "(payment"));
    CHECK(throws(idx, "payment)"));
    CHECK(throws(idx, "AND payment"));
    CHECK(throws(idx, "payment OR"));
    CHECK(throws(idx, "NOT"));
    CHECK(throws(idx, "---"));

    // empty index
    Index empty;
    CHECK(runQuery(empty, "anything").empty());
    CHECK(runQuery(empty, "NOT anything").empty());

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}