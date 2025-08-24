#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include <string>
#include <vector>

namespace utils {
    void Tokenize(const char* src, const char* delim, std::vector<std::string>& tokens);
    void ErrorCodeToString(DWORD code, std::string& str);
}