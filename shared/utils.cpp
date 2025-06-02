#include "shared/utils.h"

namespace utils {
    void Tokenize(const char* src, const char* delim, std::vector<std::string>& tokens) {
		const char* start = src;
		const char* token = nullptr;
		while (token = strpbrk(start, delim)) {
			if (start != token) {
				tokens.emplace_back(start, token);
			}

			start = token + 1;
		}

		if (*(start + 1)) {
			tokens.emplace_back(start);
		}
    }
}