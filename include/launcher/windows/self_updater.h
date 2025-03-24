#pragma once

#include "launcher/widgets/text_ctrl_log_widget.h"
#include "shared/loggable_gui.h"
#include "wx/wx.h"

class SelfUpdaterWindow : public wxFrame {
public:
    SelfUpdaterWindow();

    /* Checks for an available update.If one is found, prompts the user to ask if they want to update.
    * Returns true if the updater exe was successfully launched. In this case, the launcher should shut down asap.
    */ 
    bool HandleSelfUpdate();

private:
    bool PromptSelfUpdate(std::string const& version, std::string const& url);

    void Initialize();

    wxTextCtrl* _mainText = nullptr;
    wxTextCtrlLog _logWindow;
};