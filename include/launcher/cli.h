#pragma once

#include <optional>
#include "wx/wxchar.h"

#include "launcher/launcher_configuration.h"

class CLIParser {
public:
    int Parse(int argc, wxChar** argv);

    inline bool ForceWizard() const {
        return _forceWizard;
    }

    inline bool ForceRepentogonUpdate() const {
        return _forceRepentogonUpdate;
    }

    inline bool RepentogonInstallerWait() const {
        return _repentogonInstallerWaitOnFinished;
    }

    inline unsigned long RepentogonInstallerRefreshRate() const {
        return _repentogonInstallerRefreshRate;
    }

    inline bool SkipSelfUpdate() const {
        return _skipSelfUpdate;
    }

    inline static CLIParser* instance() {
        static CLIParser parser;
        return &parser;
    }

    inline unsigned long CurlLimit() const {
        return _curlLimit;
    }

    inline unsigned long CurlTimeout() const {
        return _curlTimeout;
    }

    inline unsigned long CurlConnectTimeout() const {
        return _curlConnectTimeout;
    }

    inline bool SteamLaunch() const {
        return _steamLaunch;
    }

    inline std::string const& IsaacPath() const {
        return _isaacPath;
    }

    inline bool LuaDebug() const {
        return _luaDebug;
    }

    inline std::string const& LuaHeapSize() const {
        return _luaHeapSize;
    }

    inline bool NetworkTest() const {
        return _networkTest;
    }

    inline std::optional<unsigned long> const& Stage() const {
        return _stage;
    }

    inline std::optional<unsigned long> const& StageType() const {
        return _stageType;
    }

    inline std::optional<unsigned long> const& LoadRoom() const {
        return _loadRoom;
    }

    inline std::optional<std::string> const& ConfigurationPath() const {
        return _configurationPath;
    }

    inline LaunchMode GetLaunchMode() const {
        return _launchMode;
    }

    inline bool RepentogonConsole() const {
        return _repentogonConsole;
    }

    inline bool UnstableUpdates() const {
        return _unstableUpdates;
    }

    inline bool AutomaticUpdates() const {
        return _automaticUpdates;
    }

private:
    CLIParser();

    struct Options {
        static constexpr unsigned long _repentogonInstallerDefaultRefreshRate = 100;
        static constexpr const char* forceWizard = "force-wizard";
        static constexpr const char* forceRepentogonUpdate = "force-repentogon-update";
        static constexpr const char* repentogonInstallerWait = "repentogon-installer-wait";
        static constexpr const char* repentogonInstallerRefresh = "repentogon-installer-refresh";
        static constexpr const char* skipSelfUpdate = "skip-self-update";
        static constexpr const char* curlLimit = "curl-limit";
        static constexpr const char* curlTimeout = "curl-timeout";
        static constexpr const char* curlConnectTimeout = "curl-connect-timeout";
        static constexpr const char* configurationPath = "configuration-file";

        // Start from Steam options
        static constexpr const char* steam = "steam";
        static constexpr const char* isaacPath = "isaac";
        static constexpr const char* luaDebug = "luadebug";
        static constexpr const char* luaHeapSize = "luaheapsize";
        static constexpr const char* networkTest = "networktest";
        static constexpr const char* setStage = "set-stage";
        static constexpr const char* setStageType = "set-stage-type";
        static constexpr const char* loadRoom = "load-room";
        static constexpr const char* launchMode = "launch-mode";
        static constexpr const char* repentogonConsole = "console";
        static constexpr const char* unstableUpdates = "unstable-updates";
        static constexpr const char* automaticUpdates = "auto-update";
    };

    bool _forceWizard = false;
    bool _forceRepentogonUpdate = false;
    bool _repentogonInstallerWaitOnFinished = false;
    bool _skipSelfUpdate = false;
    unsigned long _repentogonInstallerRefreshRate = Options::_repentogonInstallerDefaultRefreshRate;
    unsigned long _curlLimit = 0;
    unsigned long _curlTimeout = 0;
    unsigned long _curlConnectTimeout = 0;
    std::optional<std::string> _configurationPath;

    bool _steamLaunch = false;
    LaunchMode _launchMode = LAUNCH_MODE_REPENTOGON;
    std::string _isaacPath;
    bool _luaDebug = false;
    std::string _luaHeapSize;
    bool _networkTest = false;
    std::optional<unsigned long> _stage;
    std::optional<unsigned long> _stageType;
    std::optional<unsigned long> _loadRoom;
    bool _repentogonConsole = false;
    bool _unstableUpdates = false;
    bool _automaticUpdates = true;
};

#define sCLI CLIParser::instance()