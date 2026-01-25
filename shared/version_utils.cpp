#include "shared/version_utils.h"

#include <vector>
#include <string>
#include <algorithm>
#include <regex>

#include "shared/logger.h"

LauncherVersion::LauncherVersion(const std::string& s) : _rawString(s), _normalizedString(s) {
	if (_rawString.empty())
		return;

	// Normalize to lowercase
	std::transform(_normalizedString.begin(), _normalizedString.end(), _normalizedString.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });

	if (_normalizedString == "dev") {
		_isDev = true;
		return;
	}

	// Trim leading v
	if (_normalizedString[0] == 'v')
		_normalizedString.erase(0, 1);

	// Split into tokens
	std::regex re(R"([.\-_]+)");
	std::sregex_token_iterator first{ _normalizedString.begin(), _normalizedString.end(), re, -1 }, last;
	std::vector<std::string> tokens{ first, last };

	for (const std::string& token : tokens) {
		if (token == "beta") {
			_isBeta = true;
		} else if (token == "unstable") {
			_isUnstable = true;
		} else {
			try {
				_numbers.push_back(std::stoi(token));
			} catch (...) {
				Logger::Error("LauncherVersion: Unrecognized token `%s` found in version string `%s`\n", token.c_str(), _rawString.c_str());
			}
		}
	}
}

bool LauncherVersion::operator<(const LauncherVersion& other) const {
	// "dev" > everything else
	if (_isDev || other.IsDev()) {
		if (_isDev && other.IsDev()) {
			return false;
		}
		return !_isDev;
	}

	// compare numbers
	const size_t maxParts = std::max(NumbersSize(), other.NumbersSize());
	for (size_t i = 0; i < maxParts; ++i) {
		const int number = GetNumber(i);
		const int otherNumber = other.GetNumber(i);

		if (number != otherNumber) {
			return number < otherNumber;
		}
	}

	// same numbers then release > beta > unstable
	if (_isUnstable != other.IsUnstable()) {
		return _isUnstable;
	}
	if (_isBeta != other.IsBeta()) {
		return _isBeta;
	}

	return false;
}
