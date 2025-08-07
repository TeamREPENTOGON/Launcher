#pragma once

#include <wx/wx.h>
#include <wx/listbox.h>
#include <wx/textctrl.h>
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <launcher/windows/launcher.h>

namespace fs = std::filesystem;

struct ModInfo {
    std::string folderName;
    std::string displayName;
    std::string id;
};

class ModManagerFrame : public wxFrame { //Thought of making it a dialog, but I dont think it makes that much sense here
public:
    ModManagerFrame(wxWindow* parent, Launcher::Installation* Instalation);
    void RefreshLists();

private:
    wxListBox* _enabledlist;
    wxListBox* _disabledlist;
    wxTextCtrl* _searchctrl;
    fs::path _modspath;

    std::vector<ModInfo> allMods;
    std::unordered_map<std::string, ModInfo> modMap;

    void LoadModsFromFolder();

    void OnEnableAll(wxCommandEvent& event);
    void OnDisableAll(wxCommandEvent& event);
    void OnDoubleClickEnabled(wxCommandEvent& event);
    void OnDoubleClickDisabled(wxCommandEvent& event);
    void OnSearch(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnLoad(wxCommandEvent& event);
    void OnClose(wxCommandEvent& event);

    void EnableMod(const std::string& modfolder);
    void DisableMod(const std::string& modfolder);
    bool IsDisabled(const std::string& modfolder);

    wxDECLARE_EVENT_TABLE();
};
