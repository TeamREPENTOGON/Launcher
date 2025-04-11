#include <WinSock2.h>
#include <Windows.h>

#include "launcher/cli.h"
#include "wx/cmdline.h"

int CLIParser::Parse(int argc, wxChar** argv) {
    wxCmdLineParser parser(argc, argv);
    // parser.AddSwitch(helpShort, help, "Display this help", wxCMD_LINE_OPTION_HELP);
    parser.AddLongSwitch(forceWizard, "Force the wizard to run, even if it should not");
    parser.AddLongSwitch(forceRepentogonUpdate, "Force a Repentogon update at startup");
    int result = parser.Parse();

    if (result > 0)
        return result;

    if (parser.Found(forceWizard)) {
        _forceWizard = true;
    }

    if (parser.Found(forceRepentogonUpdate)) {
        _forceRepentogonUpdate = true;
    }

    return result;
}