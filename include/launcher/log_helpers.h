#include <map>

#include "launcher/installation.h"
#include "launcher/installation_manager.h"
#include "wx/textctrl.h"

using Notifications = Launcher::RepentogonInstallationNotification;

class NotificationVisitor {
public:
    NotificationVisitor(wxTextCtrl* text);

    void operator()(Notifications::FileDownload const& download);
    void operator()(Notifications::FileRemoval const& removal);
    void operator()(Notifications::GeneralNotification const& general);

private:
    wxTextCtrl* _text = nullptr;
    std::map<uint32_t, uint32_t> _downloadedSizePerFile;
    wxString _lastDownloadedString;
    bool _wasLastDownload = false;
    uint32_t _lastDownloadedId = 0;
};

namespace Launcher {
    void DumpRepentogonInstallationState(Launcher::Installation const* installation,
        Launcher::RepentogonInstaller const& installer, wxTextCtrl* text);
    void DisplayRepentogonFilesVersion(Launcher::Installation const* installation,
        int tabs, bool isUpdate, wxTextCtrl* text);
    void DebugDumpBrokenRepentogonInstallation(Launcher::Installation const* installation,
        wxTextCtrl* text);
}