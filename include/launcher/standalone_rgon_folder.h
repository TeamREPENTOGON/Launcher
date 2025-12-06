#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include "shared/logger.h"

namespace standalone_rgon {
    bool CopyFiles(const std::string& basePath,
        const std::string& destination);
    bool Patch(const std::string& repentogonFolder,
        const std::string& patchPath);
    bool CreateSteamAppIDFile(const std::string& repentogonFolder);

    bool GenerateRepentogonPath(const std::string& base,
        std::filesystem::path& result, bool strict);

    bool IsStandaloneFolder(const std::string& path);
    bool CreateFuckMethodFile(std::string const& base, uint32_t method);

    extern uint32_t REPENTOGON_FUCK_METHOD;
    extern std::string REPENTOGON_FUCK_METHOD_FILE;
}