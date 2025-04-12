#pragma once

#include "wx/wxchar.h"

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
    };

    bool _forceWizard = false;
    bool _forceRepentogonUpdate = false;
    bool _repentogonInstallerWaitOnFinished = false;
    bool _skipSelfUpdate = false;
    unsigned long _repentogonInstallerRefreshRate = Options::_repentogonInstallerDefaultRefreshRate;
    unsigned long _curlLimit = 0;
    unsigned long _curlTimeout = 0;
};

#define sCLI CLIParser::instance()