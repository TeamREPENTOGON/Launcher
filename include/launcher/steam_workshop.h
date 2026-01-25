#pragma once

#include <string>

#include "steam_api.h"

namespace SteamWorkshop {

extern const PublishedFileId_t REPENTOGON_WORKSHOP_ID;

bool CreateSteamEntryDirFile();

bool SubscribeAndDownload(PublishedFileId_t workshopId, std::string& outPath);
bool SubscribeDownloadAndGetFile(PublishedFileId_t workshopId, const std::string& relativeFilePath, std::string& outFullPath);

}
