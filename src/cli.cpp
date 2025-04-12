#include <WinSock2.h>
#include <Windows.h>

#include "launcher/cli.h"
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
    int result = parser.Parse();

    if (result > 0)
        return result;

    if (parser.Found(Options::forceWizard)) {
        _forceWizard = true;
    }

    if (parser.Found(Options::forceRepentogonUpdate)) {
        _forceRepentogonUpdate = true;
    }

    if (parser.Found(Options::repentogonInstallerWait)) {
        _repentogonInstallerWaitOnFinished = true;
    }

    long refreshRate = 0;
    if (parser.Found(Options::repentogonInstallerRefresh, &refreshRate)) {
        if (refreshRate <= 0) {
            refreshRate = Options::_repentogonInstallerDefaultRefreshRate;
        }

        _repentogonInstallerRefreshRate = refreshRate;
    }

    if (parser.Found(Options::skipSelfUpdate)) {
        _skipSelfUpdate = true;
    }

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

    return result;
}