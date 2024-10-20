#include <cstdarg>

#include "unpacker/logger.h"

namespace Logger {
	FILE* logFile = stdout;

	void Log(const char* prefix, const char* fmt, va_list va) {
		fprintf(logFile, prefix);
		vfprintf(logFile, fmt, va);
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

	void Error(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		Log("[ERROR] ", fmt, va);
		va_end(va);
	}
}