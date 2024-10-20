#pragma once

class ILoggableGUI {
public:
	virtual void Log(const char* prefix, bool nl, const char* fmt, ...) = 0;
	virtual void Log(const char*, ...) = 0;
	virtual void LogInfo(const char*, ...) = 0;
	virtual void LogNoNL(const char*, ...) = 0;
	virtual void LogWarn(const char*, ...) = 0;
	virtual void LogError(const char*, ...) = 0;
};