#pragma once

#include <cstdio>

namespace Logger {
	extern FILE* logFile;

	void Init(const char* path);
	void Info(const char* fmt, ...);
	void Error(const char* fmt, ...);
}