#pragma once

#include <mutex>

#include "shared/loggable_gui.h"
#include "wx/textctrl.h"

class wxTextCtrlLog : public ILoggableGUI {
public:
    wxTextCtrlLog(wxTextCtrl* ctrl);

    void Log(const char* prefix, bool nl, const char* fmt, ...);
    void Log(const char*, ...);
    void LogInfo(const char*, ...);
    void LogNoNL(const char*, ...);
    void LogWarn(const char*, ...);
    void LogError(const char*, ...);

    wxTextCtrl* Get();

private:
    std::mutex _logMutex;
    wxTextCtrl* _control;
};