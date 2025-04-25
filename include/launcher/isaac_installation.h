#pragma once

#include <optional>
#include <string>
#include <variant>

#include "shared/loggable_gui.h"

struct Version {
    /* Hash of a given version. */
    const char* hash;
    /* User-friendly string associated with this version. */
    const char* version;
    /* Whether the version is compatible with Repentogon or not. */
    bool valid;
};

enum ReadVersionStringResult {
    READ_VERSION_STRING_OK,
    READ_VERSION_STRING_ERR_NO_FILE,
    READ_VERSION_STRING_ERR_OPEN,
    READ_VERSION_STRING_ERR_NO_MEM,
    READ_VERSION_STRING_ERR_TOO_BIG,
    READ_VERSION_STRING_ERR_INVALID_PE,
    READ_VERSION_STRING_ERR_NO_VERSION
};

/* Global array of all known versions of the game. */
extern Version const knownVersions[];

class IsaacInstallation {
public:
    IsaacInstallation(ILoggableGUI* gui);
    bool Validate(std::string const& exePath);
    std::optional<std::string> AutoDetect();

    inline bool IsCompatibleWithRepentogon() const {
        return _isCompatibleWithRepentogon;
    }

    inline bool IsValid() const {
        return _isValid;
    }

    inline const char* GetVersion() const {
        if (std::holds_alternative<const Version*>(_version)) {
            const Version* version = std::get<const Version*>(_version);
            if (!version)
                return "";
            else
                return version->version;
        } else {
            return std::get<std::string>(_version).c_str();
        }
    }

    inline std::string const& GetExePath() const {
        return _exePath;
    }

    inline std::string const& GetFolderPath() const {
        return _folderPath;
    }

private:
    static std::optional<std::string> ComputeVersion(std::string const& path);
    // static const Version* GetVersionFromHash(std::string const& path);
    static std::optional<std::string> GetVersionStringFromMemory(std::string const& path);
    bool CheckCompatibilityWithRepentogon();
    static std::string StripVersion(std::string const& version);

    bool ValidateExecutable(std::string const& exePath);

    mutable ILoggableGUI* _gui;
    std::string _exePath;
    std::string _folderPath;
    std::variant<const Version*, std::string> _version;
    bool _isCompatibleWithRepentogon = false;
    bool _isValid = false;
    bool _needspatch = false;
};