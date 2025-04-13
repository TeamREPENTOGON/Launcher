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
    parser.AddLongSwitch(Options::forceWizard, "Force the wizard to run, even if it should not");
    parser.AddLongSwitch(Options::forceRepentogonUpdate, "Force a Repentogon update at startup");
    parser.AddLongSwitch(Options::repentogonInstallerWait, "Prevent the Repentogon installer from closing after "
        "installation is complete");
    parser.AddLongOption(Options::repentogonInstallerRefresh, "Time (in milliseconds) between two updates of the "
        "installer window", wxCMD_LINE_VAL_NUMBER);
    parser.AddLongSwitch(Options::skipSelfUpdate, "Skip the self update checks at startup");
    parser.AddLongOption(Options::curlLimit, "Limit (in bytes per second) for the curl operations",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongOption(Options::curlTimeout, "Timeout (in milliseconds) for curl transfers",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongOption(Options::curlConnectTimeout, "Timeout (in milliseconds) when curl opens a connection",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongOption(Options::configurationPath, "Path to the configuration file");

    parser.AddLongSwitch(Options::steam, "Perform a Steam launch, bypassing as much of the "
        "startup logic as possible");
    parser.AddLongOption(Options::isaacPath, "Absolute path to the Isaac executable to start");
    parser.AddLongSwitch(Options::luaDebug, "Enable system functions in the Lua API");
    parser.AddLongOption(Options::luaHeapSize, "Size of the Lua heap in MB");
    parser.AddLongSwitch(Options::networkTest, "Enable multiplayer features");
    parser.AddLongOption(Options::setStage, "Start stage identifier",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongOption(Options::setStageType, "Start stage variant identifier",
        wxCMD_LINE_VAL_NUMBER);
    parser.AddLongOption(Options::loadRoom, "ID of the room in which to start the run",
        wxCMD_LINE_VAL_NUMBER);
    int result = parser.Parse();

    if (result > 0)
        return result;

    _forceWizard = parser.Found(Options::forceWizard);
    _forceRepentogonUpdate = parser.Found(Options::forceRepentogonUpdate);
    _repentogonInstallerWaitOnFinished = parser.Found(Options::repentogonInstallerWait);

    long refreshRate = 0;
    if (parser.Found(Options::repentogonInstallerRefresh, &refreshRate)) {
        if (refreshRate <= 0) {
            refreshRate = Options::_repentogonInstallerDefaultRefreshRate;
        }

        _repentogonInstallerRefreshRate = refreshRate;
    }

    _skipSelfUpdate = parser.Found(Options::skipSelfUpdate);

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

    wxString configurationPath;
    if (parser.Found(Options::configurationPath, &configurationPath)) {
        _configurationPath = configurationPath;
    }

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
            _setStage = stage;
        }
    }

    long stageType;
    if (parser.Found(Options::setStageType, &stageType)) {
        if (stageType >= 0) {
            _setStageType = stageType;
        }
    }

    long loadRoom;
    if (parser.Found(Options::loadRoom, &loadRoom)) {
        _loadRoom = loadRoom;
    }

    return result;
}