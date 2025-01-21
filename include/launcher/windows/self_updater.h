#pragma once

#include "launcher/widgets/text_ctrl_log_widget.h"
#include "launcher/self_updater/launcher_update_manager.h"
#include "shared/loggable_gui.h"
#include "wx/wx.h"

class SelfUpdaterWindow : public wxFrame {
public:
    SelfUpdaterWindow();
    bool HandleSelfUpdate();

private:
    bool PromptSelfUpdate(std::string const& version, std::string const& url);
    bool DoSelfUpdate(std::string const& version, std::string const& url);

    void Initialize();

    wxTextCtrl* _mainText = nullptr;
    wxTextCtrlLog _logWindow;
    Updater::LauncherUpdateManager _updateManager;
};