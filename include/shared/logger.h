#pragma once

#include <cstdarg>
#include <cstdio>

class Logger {
public:
	static void Info(const char* fmt, ...);
	static void Warn(const char* fmt, ...);
	static void Error(const char* fmt, ...);
	static void Fatal(const char* fmt, ...);
	static void Memory(const char* ctx);

	static void Init(const char* filename, bool append);

private:
	static void Log(const char* prefix, const char* fmt, va_list va);
	static FILE* _file;
	static const char* _memoryError;
};