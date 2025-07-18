#include <atomic>

#include <Windows.h>

#include "shared/logger.h"
#include "shared/steam.h"

static std::atomic<char*> __steamInstallationPath = nullptr;

char* Steam::GetSteamInstallationPath() {
	if (char* path = __steamInstallationPath.load(std::memory_order_acquire)) {
		return path;
	}

	DWORD valueType = 0;
	char buffer[4096];
	DWORD bufferLen = 4096;
	DWORD result = RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath",
		RRF_RT_REG_SZ, &valueType, buffer, &bufferLen);
	if (result == ERROR_MORE_DATA) {
		Logger::Error("Steam::GetSteamInstallationPath: steam installation path is abnormally long\n");
		return nullptr;
	} else if (result == ERROR_FILE_NOT_FOUND) {
		Logger::Warn("Steam::GetSteamInstallationPath: attempting Wow6432Node as alternative\n");
		result = RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Wow6432Node\\Valve\\Steam", "InstallPath",
			RRF_RT_REG_SZ, &valueType, buffer, &bufferLen);
		if (result != 0) {
			Logger::Error("Steam::GetSteamInstallationPath: error %d while querying Wow6432Node\n", result);
			return nullptr;
		}
	} else if (result != 0) {
		Logger::Error("Steam::GetSteamInstallationPath: error %d while querying installation path\n", result);
		return nullptr;
	}

	char* resultStr = (char*)malloc(bufferLen);
	if (!resultStr) {
		Logger::Error("Steam::GetSteamInstallationPath: unable to allocate memory to store path\n");
		return nullptr;
	}

	strcpy(resultStr, buffer);

	char* expected = nullptr;
	if (!__steamInstallationPath.compare_exchange_strong(expected, resultStr, std::memory_order_acq_rel, std::memory_order_acquire)) {
		free(resultStr);
		return expected;
	}

	return resultStr;
}

void Steam::FreeMemory() {
	free(__steamInstallationPath);
}