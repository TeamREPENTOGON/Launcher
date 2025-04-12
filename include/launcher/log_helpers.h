#include <map>

#include "launcher/installation.h"
#include "launcher/repentogon_installer.h"
#include "wx/textctrl.h"

using Notifications = Launcher::RepentogonInstallationNotification;

class NotificationVisitor {
public:
    NotificationVisitor(wxTextCtrl* text, unsigned long refreshRate);

    void operator()(Notifications::FileDownload const& download);
    void operator()(Notifications::FileRemoval const& removal);
    void operator()(Notifications::GeneralNotification const& general);

    void NotifyAllDownloads(bool checkFreshness);

private:
    struct DownloadData {
        uint32_t id;
        uint32_t size;
        std::string filename;
        uint32_t lastLoggedSize;
        std::chrono::steady_clock::time_point last;
    };

    void NotifyDownload(DownloadData& data, bool ignoreRefresh, bool finalize);

    std::mutex _mutex;
    wxTextCtrl* _text = nullptr;
    std::map<uint32_t, DownloadData> _downloadData;
    wxString _lastDownloadedString;
    bool _wasLastDownload = false;
    uint32_t _lastDownloadedId = 0;
    long _lastInsertionPoint = 0;
    unsigned long _refreshRate = 500;
};

namespace Launcher {
    void DumpRepentogonInstallationState(Launcher::Installation const* installation,
        Launcher::RepentogonInstaller const& installer, wxTextCtrl* text);
    void DisplayRepentogonFilesVersion(Launcher::Installation const* installation,
        int tabs, bool isUpdate, wxTextCtrl* text);
    void DebugDumpBrokenRepentogonInstallation(Launcher::Installation const* installation,
        wxTextCtrl* text);
}