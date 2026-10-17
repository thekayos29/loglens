#pragma once
#include <stdexcept>
#include <string>

#include "index.h"

struct QueryError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Set operations on sorted posting lists (two-pointer merges, O(n+m)).
Postings intersect(const Postings& a, const Postings& b);   // AND
Postings unite(const Postings& a, const Postings& b);       // OR
Postings difference(const Postings& a, const Postings& b);  // a AND NOT b

// Grammar (NOT binds tighter than AND, AND tighter than OR):
//   or      := and ( "OR" and )*
//   and     := not ( ["AND"] not )*      (adjacent terms mean AND)
//   not     := "NOT" not | primary
//   primary := WORD | "(" or ")"
// Operators must be UPPERCASE; "and" in lowercase is just a search word.
// Throws QueryError on malformed queries.
Postings runQuery(const Index& index, const std::string& query);