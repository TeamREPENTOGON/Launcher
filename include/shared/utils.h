#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace utils {
    void Tokenize(const char* src, const char* delim, std::vector<std::string>& tokens);
    void ErrorCodeToString(unsigned long code, std::string& str);
	std::string ConvertToUTF8(std::wstring_view wstr);
	std::filesystem::path GetUserProfileDir();
	std::filesystem::path GetUserMyGamesDir();
}