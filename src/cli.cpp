#include <WinSock2.h>
#include <Windows.h>

#include "launcher/cli.h"
#include "shared/logger.h"
#include "wx/cmdline.h"

int CLIParser::Parse(int argc, wxChar** argv) {
    wxCmdLineParser parser(argc, argv);
    // parser.AddSwitch(helpShort, help, "Display this help", wxCMD_LINE_OPTION_HELP);
    parser.AddLongSwitch(forceWizard, "Force the wizard to run, even if it should not");
    parser.AddLongSwitch(forceRepentogonUpdate, "Force a Repentogon update at startup");
    parser.AddLongSwitch(repentogonInstallerWait, "Prevent the Repentogon installer from closing after "
        "installation is complete");
    parser.AddLongOption(repentogonInstallerRefresh, "Time (in milliseconds) between two updates of the "
        "installer window", wxCMD_LINE_VAL_NUMBER);
    parser.AddLongSwitch(skipSelfUpdate, "Skip the self update checks at startup");
    int result = parser.Parse();

    if (result > 0)
        return result;

    if (parser.Found(forceWizard)) {
        _forceWizard = true;
    }

    if (parser.Found(forceRepentogonUpdate)) {
        _forceRepentogonUpdate = true;
    }

    if (parser.Found(repentogonInstallerWait)) {
        _repentogonInstallerWaitOnFinished = true;
    }

    long refreshRate = 0;
    if (parser.Found(repentogonInstallerRefresh, &refreshRate)) {
        if (refreshRate <= 0) {
            refreshRate = _repentogonInstallerDefaultRefreshRate;
        }

        _repentogonInstallerRefreshRate = refreshRate;
    }

    if (parser.Found(skipSelfUpdate)) {
        _skipSelfUpdate = true;
    }

    return result;
}