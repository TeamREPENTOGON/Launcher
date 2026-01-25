#pragma once

#include <vector>
#include <string>

class LauncherVersion {
  public:
	LauncherVersion(const std::string& versionstring);
	LauncherVersion() = delete;

	size_t NumbersSize() const { return _numbers.size(); }
	int GetNumber(const int idx) const {
		if (idx >= 0 && idx < (int)_numbers.size()) {
			return _numbers[idx];
		}
		return 0;
	}

	bool IsBeta() const { return _isBeta; }
	bool IsUnstable() const  { return _isUnstable; }
	bool IsDev() const { return _isDev; }

	bool operator<(const LauncherVersion& other) const;

	bool operator>(const LauncherVersion& other) const {
		return other < *this;
	}
	bool operator>=(const LauncherVersion& other) const {
		return !(*this < other);
	}
	bool operator<=(const LauncherVersion& other) const {
		return !(other < *this);
	}
	bool operator==(const LauncherVersion& other) const {
		return !(*this < other) && !(other < *this);
	}

  private:
	std::string _rawString;
	std::string _normalizedString;
	std::vector<int> _numbers;
	bool _isBeta = false;
	bool _isUnstable = false;
	bool _isDev = false;
};
