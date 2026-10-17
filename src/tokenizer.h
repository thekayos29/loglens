#pragma once
#include <string>
#include <vector>

// Splits text into lowercase alphanumeric words.
// "ERROR payment failed: timeout" -> {"error","payment","failed","timeout"}
std::vector<std::string> tokenize(const std::string& line);