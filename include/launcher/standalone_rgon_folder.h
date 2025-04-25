#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include "shared/logger.h"

namespace fs = std::filesystem; //so I dont go insane
extern std::string tocopy[];
extern bool copyFiles(const std::string& basePath, bool patch);
extern fs::path getOGExePath(const fs::path& exePath);
extern fs::path getCopyExePath(const fs::path& exePath);
extern bool exeIsCopy(const fs::path& exePath);
extern bool exeCopyExists(const fs::path& exePath);