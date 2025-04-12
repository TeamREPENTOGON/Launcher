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

private:
    CLIParser();

    bool _forceWizard = false;
    bool _forceRepentogonUpdate = false;
    bool _repentogonInstallerWaitOnFinished = false;
    bool _skipSelfUpdate = false;
    unsigned long _repentogonInstallerRefreshRate = _repentogonInstallerDefaultRefreshRate;

    static constexpr unsigned long _repentogonInstallerDefaultRefreshRate = 100;
    static constexpr const char* forceWizard = "force-wizard";
    static constexpr const char* forceRepentogonUpdate = "force-repentogon-update";
    static constexpr const char* repentogonInstallerWait = "repentogon-installer-wait";
    static constexpr const char* repentogonInstallerRefresh = "repentogon-installer-refresh";
    static constexpr const char* skipSelfUpdate = "skip-self-update";
    /* static constexpr const char* help = "help";
    static constexpr const char* helpShort = "h"; */
};

#define sCLI CLIParser::instance()