#include "launcher/steam_workshop.h"

#include <Windows.h>
#include <WinBase.h>
#include <string>
#include <filesystem>
#include <fstream>

#include "steam_api.h"
#include "synchapi.h"
#include "shared/logger.h"
#include <curl/curl.h>

namespace SteamWorkshop {

// "REPENTOGON Download Helper", a workshop entry that contains the latest versions of
// REPENTOGON and the launcher, for getting updates if GitHub connection fails.
const PublishedFileId_t REPENTOGON_WORKSHOP_ID = 3643104060;

bool WaitForWorkshopDownload(PublishedFileId_t fileId, uint32 timeoutMs = 60000) {
	Logger::Info("Checking download for steam workshop item %llu...\n", fileId);


	if (!CanReachSteamWorkshop()) {
		Logger::Error("Cannot connect to the workshop!.\n");
		return false;
	}

	uint32 elapsed = 0;
	const uint32 step = 100;

	while (elapsed < timeoutMs) {
		SteamAPI_RunCallbacks();

		uint32 state = SteamUGC()->GetItemState(fileId);

		if (state & k_EItemStateInstalled) {
			Logger::Info("Workshop item up-to-date.\n", fileId);
			return true;
		}

		if (state & k_EItemStateDownloadPending ||
			state & k_EItemStateDownloading) {
			Sleep(step);
			elapsed += step;
			continue;
		}
		SteamUGC()->DownloadItem(fileId, true);
		Sleep(step);
		elapsed += step;
	}

	return false;
}

bool SubscribeAndDownload(PublishedFileId_t workshopId, std::string& outPath) {
	if (!SteamAPI_Init() || !SteamUGC()) {
		Logger::Error("Steam is not available.\n");
		return false;
	}


	SteamUGC()->SubscribeItem(workshopId);
	if (!WaitForWorkshopDownload(workshopId)) {
		Logger::Error("Failure waiting for workshop download.\n");
		return false;
	}

	uint64 sizeOnDisk = 0;
	char installPath[1024] = {};
	uint32 timeStamp = 0;

	if (!SteamUGC()->GetItemInstallInfo(
			workshopId,
			&sizeOnDisk,
			installPath,
			sizeof(installPath),
			&timeStamp)) {
		Logger::Error("GetItemInstallInfo failed.\n");
		return false;
	}

	outPath = std::string(installPath);

	if (workshopId == REPENTOGON_WORKSHOP_ID && !std::filesystem::exists(outPath + "/disable.it")) {
		std::ofstream(outPath + "/disable.it");
	}

	return true;
}

bool SubscribeDownloadAndGetFile(PublishedFileId_t workshopId, const std::string& relativeFilePath, std::string& outFullPath) {
	std::string installPath;
	if (!SubscribeAndDownload(workshopId, installPath)) {
		return false;
	}

	outFullPath = installPath + "/" + relativeFilePath;

	if (GetFileAttributesA(outFullPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		Logger::Error("Downloaded workshop item, but file `%s` does not exist.\n", outFullPath.c_str());
		return false;
	}

	return true;
}


bool CanReachSteamWorkshop()
{
	CURL* curl = curl_easy_init();
	if (!curl)
		return false;

	curl_easy_setopt(curl, CURLOPT_URL, "https://steamcommunity.com/");
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);          
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);         //5sec timeout
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  

	CURLcode res = curl_easy_perform(curl);

	long response_code = 0;
	if (res == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	curl_easy_cleanup(curl);
	return (res == CURLE_OK && response_code >= 200 && response_code < 400);
}

//this is mainly for the updater to find later kind of hacky, but its the only way I could think of where the updater can use the steam entry when available without the steamapi (wont be able to make it available if it isnt tho)
bool CreateSteamEntryDirFile() {
	std::string installPath;
	if (!SubscribeAndDownload(REPENTOGON_WORKSHOP_ID, installPath)) {
		return false;
	}
	std::ofstream("steamentrydir.txt") << installPath;
	return true;
}

}  // namespace SteamWorkshop
