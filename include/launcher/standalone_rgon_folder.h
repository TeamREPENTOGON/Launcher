#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include "shared/logger.h"

namespace standalone_rgon {
    bool copyFiles(const std::string& basePath, bool patch);
    std::filesystem::path getOGExePath(const std::filesystem::path& exePath);
    std::filesystem::path getCopyExePath(const std::filesystem::path& exePath);
    bool exeIsCopy(const std::filesystem::path& exePath);
    bool exeCopyExists(const std::filesystem::path& exePath);
}