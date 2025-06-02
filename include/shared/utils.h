#pragma once

#include <string>
#include <vector>

namespace utils {
    void Tokenize(const char* src, const char* delim, std::vector<std::string>& tokens);
}