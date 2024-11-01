#include <cstdarg>
#include <cstdlib>
#include <ctime>

#include "unpacker/logger.h"

namespace Logger {
	FILE* logFile = stdout;

	void Log(const char* prefix, const char* fmt, va_list va) {
		time_t now = time(nullptr);
		tm* nowTm = localtime(&now);
		char timeBuffer[4096];
		strftime(timeBuffer, 4095, "[%Y-%m-%d %H:%M:%S] ", nowTm);

		fprintf(logFile, prefix);
		fprintf(logFile, timeBuffer);
		vfprintf(logFile, fmt, va);
		fflush(logFile);
	}

	void Init(const char* name) {
		logFile = fopen(name, "a");
		if (!logFile) {
			logFile = stdout;
		}
	}

	void Info(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		Log("[INFO] ", fmt, va);
		va_end(va);
	}

	void Warn(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		Log("[WARN] ", fmt, va);
		va_end(va);
	}

	void Error(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		Log("[ERROR] ", fmt, va);
		va_end(va);
	}

	void Fatal(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		Log("[FATAL] ", fmt, va);
		va_end(va);
		abort();
	}
}