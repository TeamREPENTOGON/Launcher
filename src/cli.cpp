#include <WinSock2.h>
#include <Windows.h>

#include "launcher/cli.h"
#include "shared/filesystem.h"
#include "shared/logger.h"
#include "wx/cmdline.h"

CLIParser::CLIParser() {

}

int CLIParser::Parse(int argc, wxChar** argv) {
    wxCmdLineParser parser(argc, argv);
    // parser.AddSwitch(helpShort, help, "Display this help", wxCMD_LINE_OPTION_HELP);
    parser.AddLongSwitch(Options::skipWizard, "Skip the wizard, even if it should run");
    parser.AddLongSwitch(Options::forceWizard, "Force the wizard to run, even if it should not");
    parser.AddLongSwitch(Options::skipRepentogonUpdate, "Skip the Repentogon update");
    parser.AddLongSwitch(Options::forceRepentogonUpdate, "Force a Repentogon update at startup");
    parser.AddLongSwitch(Options::repentogonInstallerWait, "Prevent the Repentogon installer from closing after "
        "installation is complete");
    parser.AddLongOption(Options::repentogonInstallerRefresh, "Time (in milliseconds) between two updates of the "
        "installer window", wxCMD_LINE_VAL_NUMBER);
    parser.AddLongSwitch(Options::skipSelfUpdate, "Skip the self update checks at startup");
    parser.AddLongSwitch(Options::stealthMode, "Skip displaying launcher windows when possible, launch the game automatically if able, and close the launcher when the game closes.");
    parser.AddLongOption(Options::curlLimit, "Limit (in bytes per second) for the curl operations",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongOption(Options::curlTimeout, "Timeout (in milliseconds) for curl transfers",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongOption(Options::curlConnectTimeout, "Timeout (in milliseconds) when curl opens a connection",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongOption(Options::configurationPath, "Path to the configuration file");
    parser.AddLongSwitch(Options::trapIsaacLaunch, "Trap the launcher when starting Isaac, allowing for a debugger to attach");
    parser.AddLongOption(Options::isaacWaitTime, "Max wait time (in milliseconds) after creating the Repentogon remote thread",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongSwitch(Options::strictThreadCancel, "Crash the launcher if a cancelled thread does not enter its cancellable state");

    parser.AddLongSwitch(Options::steam, "Perform a Steam launch, bypassing as much of the "
        "startup logic as possible");
    parser.AddLongOption(Options::isaacPath, "Absolute path to the Isaac executable to start");
    parser.AddLongSwitch(Options::luaDebug, "Enable system functions in the Lua API");
    parser.AddLongOption(Options::luaHeapSize, "Size of the Lua heap (default: \"1024M\")");
    parser.AddLongSwitch(Options::networkTest, "Enable multiplayer features");
    parser.AddLongOption(Options::setStage, "Start stage identifier",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongOption(Options::setStageType, "Start stage variant identifier",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongOption(Options::loadRoom, "Path to a rooms xml file for testing");
    parser.AddLongOption(Options::launchMode, "Whether to launch vanilla or Repentogon.\n"
        "Accepted values: \"vanilla\", \"repentogon\". Default: repentogon.");
    parser.AddLongSwitch(Options::repentogonConsole, "Enable the Repentogon console window");
    parser.AddLongSwitch(Options::unstableUpdates, "Allow unstable releases when updating Repentogon");
    parser.AddLongSwitch(Options::automaticUpdates, "Allow the launcher to keep Repentogon up-to-date");

    int result = parser.Parse();

    if (result > 0)
        return result;

    _trapIsaacLaunch = parser.Found(Options::trapIsaacLaunch);

    _forceWizard = parser.Found(Options::forceWizard);
    _skipWizard = parser.Found(Options::skipWizard);

    if (_forceWizard && _skipWizard) {
        wxASSERT_MSG(false, "Cannot force and skip the wizard at the same time");
    }

    _forceRepentogonUpdate = parser.Found(Options::forceRepentogonUpdate);
    _skipRepentogonUpdate = parser.Found(Options::skipRepentogonUpdate);

    if (_forceRepentogonUpdate && _skipRepentogonUpdate) {
        wxASSERT_MSG(false, "Cannot force and skip Repentogon update at the same time");
    }

    _repentogonInstallerWaitOnFinished = parser.Found(Options::repentogonInstallerWait);

    long refreshRate = 0;
    if (parser.Found(Options::repentogonInstallerRefresh, &refreshRate)) {
        if (refreshRate <= 0) {
            refreshRate = Options::_repentogonInstallerDefaultRefreshRate;
        }

        _repentogonInstallerRefreshRate = refreshRate;
    }

    _skipSelfUpdate = parser.Found(Options::skipSelfUpdate);
    _stealthMode = parser.Found(Options::stealthMode);

    long curlSpeedLimit = 0;
    if (parser.Found(Options::curlLimit, &curlSpeedLimit)) {
        if (curlSpeedLimit > 0) {
            _curlLimit = curlSpeedLimit;
        }
    }

    long curlTimeout = 0;
    if (parser.Found(Options::curlTimeout, &curlTimeout)) {
        if (curlTimeout > 0) {
            _curlTimeout = curlTimeout;
        }
    }

    long curlConnectTimeout = 0;
    if (parser.Found(Options::curlConnectTimeout, &curlConnectTimeout)) {
        if (curlConnectTimeout > 0) {
            _curlConnectTimeout = curlConnectTimeout;
        }
    }

    long isaacWaitTime = 0;
    if (parser.Found(Options::isaacWaitTime, &isaacWaitTime)) {
        if (isaacWaitTime < 0) {
            isaacWaitTime = -1;
        }
    }
    _isaacWaitTime = isaacWaitTime;

    wxString configurationPath;
    if (parser.Found(Options::configurationPath, &configurationPath)) {
        _configurationPath = configurationPath;
    }

    _strictThreadCancel = parser.Found(Options::strictThreadCancel);

    _steamLaunch = parser.Found(Options::steam);

    wxString isaacPath;
    if (parser.Found(Options::isaacPath, &isaacPath)) {
        _isaacPath = std::move(isaacPath);
    }

    _luaDebug = parser.Found(Options::luaDebug);

    wxString heapSize;
    if (parser.Found(Options::luaHeapSize, &heapSize)) {
        _luaHeapSize = std::move(heapSize);
    }

    _networkTest = parser.Found(Options::networkTest);

    long stage;
    if (parser.Found(Options::setStage, &stage)) {
        if (stage >= 0) {
            _stage = stage;
        }
    }

    long stageType;
    if (parser.Found(Options::setStageType, &stageType)) {
        if (stageType >= 0) {
            _stageType = stageType;
        }
    }

    wxString loadRoom;
    if (parser.Found(Options::loadRoom, &loadRoom)) {
        _loadRoom = loadRoom;
    }

    wxString launchModeStr;
    if (parser.Found(Options::launchMode, &launchModeStr)) {
        if (launchModeStr == "vanilla") {
            _launchMode = LAUNCH_MODE_VANILLA;
        } else if (launchModeStr == "repentogon") {
            _launchMode = LAUNCH_MODE_REPENTOGON;
        } else {
            Logger::Error("Unknown launch mode: %s\n", launchModeStr.ToStdString().c_str());
        }
    }

    _repentogonConsole = parser.Found(Options::repentogonConsole);
    _unstableUpdates = parser.Found(Options::unstableUpdates);
    _automaticUpdates = parser.Found(Options::automaticUpdates);

    return result;
}