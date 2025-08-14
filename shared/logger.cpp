#include <WinSock2.h>
#include <Windows.h>

#include <ctime>
#include <mutex>

#include "shared/logger.h"

FILE* Logger::_file = NULL;
static std::mutex __logMutex;

void Logger::Init(const char* filename, bool append) {
	std::unique_lock<std::mutex> lck(__logMutex);
	if (!_file) {
		_file = fopen(filename, append ? "a" : "w");
		if (!_file) {
			MessageBoxA(NULL, "Unable to create log file launcher.log\nExtra log information will not be available",
				"Error",
				MB_OK | MB_ICONASTERISK | MB_TASKMODAL);
		}
	}
}

void Logger::Debug(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	Log("[DEBUG] ", fmt, va);
	va_end(va);
}

void Logger::Info(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	Log("[INFO] ", fmt, va);
	va_end(va);
}

void Logger::Warn(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	Log("[WARN] ", fmt, va);
	va_end(va);
}

void Logger::Error(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	Log("[ERROR] ", fmt, va);
	va_end(va);
}

void Logger::Critical(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	Log("[CRITICAL] ", fmt, va);
	va_end(va);
}

void Logger::Fatal(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	Log("[FATAL] ", fmt, va);
	va_end(va);
	abort();
}

void Logger::Log(const char* prefix, const char* fmt, va_list va) {
	if (!Logger::_file) {
		return;
	}

	time_t now = time(nullptr);
	tm* nowTm = localtime(&now);
	char timeBuffer[4096];
	strftime(timeBuffer, 4095, "[%Y-%m-%d %H:%M:%S] ", nowTm);

	std::unique_lock<std::mutex> lck(__logMutex);
	fprintf(_file, "%s", prefix);
	fprintf(_file, "%s", timeBuffer);
	vfprintf(_file, fmt, va);
	fflush(_file);
}

static const char* MemoryError = "[FATAL] Out of memory: ";

void Logger::Memory(const char* ctx) {
	if (!Logger::_file) {
		return;
	}

	fputs(MemoryError, _file);
	fputs(ctx, _file);
}

void Logger::End() {
	if (Logger::_file) {
		fflush(Logger::_file);
		fclose(Logger::_file);
	}
}