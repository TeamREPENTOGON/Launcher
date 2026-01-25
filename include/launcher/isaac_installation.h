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

class InstallationData {
private:
    std::string _exePath;
    std::string _folderPath;
    std::variant<const Version*, std::string> _version;
    bool _isCompatibleWithRepentogon = false;
    std::string _CompatReason = "Maybe it works?, probably not tho, try it!";
    bool _isValid = false;
    bool _needsPatch = false;
    int _vanillaexeispatchable = -1; //-1 not checked, 0 not pachable, 1 pachable

    mutable ILoggableGUI* _gui = nullptr;

    static std::optional<std::string> ComputeVersion(std::string const& path);
    static std::optional<std::string> GetVersionStringFromMemory(std::string const& path);
    static std::string StripVersion(std::string const& version);

    // Returns true if a patch is available to convert this version into a supported one.
    bool PatchIsAvailable(bool skipOnlineCheck = false);
    std::string patchtargetversion = "v1.9.7.12.J273";

public:
    inline void SetGUI(ILoggableGUI* gui) {
        _gui = gui;
    }

    bool Validate(std::string const& path, bool repentogon, bool* standalone);
    bool ValidateExecutable(std::string const& path);
    bool DoValidateExecutable(std::string const& path) const;

    inline bool IsCompatibleWithRepentogon() const {
        return _isCompatibleWithRepentogon;
    }

    inline bool NeedsPatch() const {
        return _needsPatch;
    }

    inline bool IsValid() const {
        return _isValid;
    }
    inline std::string GetReason() const {
        return _CompatReason;
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
};

class IsaacInstallation {
public:
    IsaacInstallation(ILoggableGUI* gui);

    bool Validate(std::string const& exePath, bool* standalone);
    bool ValidateRepentogon(std::string const& folderPath);
    std::optional<std::string> AutoDetect(bool* standalone);

    InstallationData const& GetMainInstallation() const {
        return _mainInstallation;
    }

    InstallationData const& GetRepentogonInstallation() const {
        return _repentogonInstallation;
    }

private:
    mutable ILoggableGUI* _gui;

    inline InstallationData& GetInstallationData(bool repentogon = false) {
        return repentogon ? _repentogonInstallation : _mainInstallation;
    }

    InstallationData _mainInstallation;
    InstallationData _repentogonInstallation;
};