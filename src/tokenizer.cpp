#include "tokenizer.h"

#include <cctype>

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : line) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            current += static_cast<char>(std::tolower(uc));
        } else if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}