#include "launcher/modmanager.h"
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <fstream>
#include <sstream>
#include <rapidxml/rapidxml.hpp>
#include <rapidxml/rapidxml_utils.hpp>
#include <filesystem>
#include <unordered_set>
#include "launcher/installation.h"

namespace fs = std::filesystem;

enum {
    WINDOW_BUTTON_MODMAN_ENABLEALL = wxID_HIGHEST + 1,
    WINDOW_BUTTON_MODMAN_DISABLEALL,
    WINDOW_INPUT_MODMAN_SEARCH,
    WINDOW_BUTTON_MODMAN_SAVE,
    WINDOW_BUTTON_MODMAN_LOAD,
    WINDOW_BUTTON_MODMAN_CLOSE,
    WINDOW_LIST_MODMAN_ENABLED,
    WINDOW_LIST_MODMAN_DISABLED
};

wxBEGIN_EVENT_TABLE(ModManagerFrame, wxFrame)
EVT_BUTTON(WINDOW_BUTTON_MODMAN_ENABLEALL, ModManagerFrame::OnEnableAll)
EVT_BUTTON(WINDOW_BUTTON_MODMAN_DISABLEALL, ModManagerFrame::OnDisableAll)
EVT_LISTBOX_DCLICK(WINDOW_LIST_MODMAN_ENABLED, ModManagerFrame::OnDoubleClickEnabled)
EVT_LISTBOX_DCLICK(WINDOW_LIST_MODMAN_DISABLED, ModManagerFrame::OnDoubleClickDisabled)
EVT_TEXT(WINDOW_INPUT_MODMAN_SEARCH, ModManagerFrame::OnSearch)
EVT_BUTTON(WINDOW_BUTTON_MODMAN_SAVE, ModManagerFrame::OnSave)
EVT_BUTTON(WINDOW_BUTTON_MODMAN_LOAD, ModManagerFrame::OnLoad)
EVT_BUTTON(WINDOW_BUTTON_MODMAN_CLOSE, ModManagerFrame::OnClose)
wxEND_EVENT_TABLE()

ModManagerFrame::ModManagerFrame(wxWindow* parent, Launcher::Installation* Instalation)
    : wxFrame(parent, wxID_ANY, "REPENTOGON Mod Manager", wxDefaultPosition, wxSize(600, 400)) {

    Center(wxBOTH);

    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* listSizer = new wxBoxSizer(wxHORIZONTAL);

    fs::path originalPath(Instalation->GetIsaacInstallation().GetMainInstallation().GetExePath());
    _modspath = originalPath.parent_path() / "mods";


    _enabledlist = new wxListBox(panel, WINDOW_LIST_MODMAN_ENABLED, wxDefaultPosition, wxSize(200, 200), 0, nullptr, wxLB_SINGLE | wxLB_SORT);
    _disabledlist = new wxListBox(panel, WINDOW_LIST_MODMAN_DISABLED, wxDefaultPosition, wxSize(200, 200), 0, nullptr, wxLB_SINGLE | wxLB_SORT);

    wxBoxSizer* leftBox = new wxBoxSizer(wxVERTICAL);
    leftBox->Add(new wxStaticText(panel, wxID_ANY, "[Enabled Mods]"), 0, wxALIGN_CENTER | wxBOLD);
    leftBox->Add(_enabledlist, 1, wxEXPAND | wxALL, 5);
    leftBox->Add(new wxButton(panel, WINDOW_BUTTON_MODMAN_DISABLEALL, "Disable All"), 0, wxEXPAND | wxALL, 5);

    wxBoxSizer* rightBox = new wxBoxSizer(wxVERTICAL);
    rightBox->Add(new wxStaticText(panel, wxID_ANY, "[Disabled Mods]"), 0, wxALIGN_CENTER | wxBOLD);
    rightBox->Add(_disabledlist, 1, wxEXPAND | wxALL, 5);
    rightBox->Add(new wxButton(panel, WINDOW_BUTTON_MODMAN_ENABLEALL, "Enable All"), 0, wxEXPAND | wxALL, 5);

    wxBoxSizer* centerButtons = new wxBoxSizer(wxVERTICAL);
    
    centerButtons->Add(new wxStaticText(panel, wxID_ANY, "<"), 0, wxALIGN_CENTER); //just for the looks, lol, they feel pointless as buttons
    centerButtons->Add(new wxStaticText(panel, wxID_ANY, ">"), 0, wxALIGN_CENTER); //just for the looks, lol, they feel pointless as buttons
    

    listSizer->Add(leftBox, 1, wxEXPAND);
    listSizer->Add(centerButtons, 0, wxALIGN_CENTER | wxALL, 10);
    listSizer->Add(rightBox, 1, wxEXPAND);

    wxBoxSizer* saveLoadBox = new wxBoxSizer(wxVERTICAL);
    saveLoadBox->Add(new wxButton(panel, WINDOW_BUTTON_MODMAN_SAVE, "Save..."), 0, wxEXPAND | wxALL, 5);
    saveLoadBox->Add(new wxButton(panel, WINDOW_BUTTON_MODMAN_LOAD, "Load..."), 0, wxEXPAND | wxALL, 5);
    listSizer->Add(saveLoadBox, 0, wxALIGN_TOP);

    topSizer->Add(listSizer, 1, wxEXPAND);

    wxBoxSizer* bottom = new wxBoxSizer(wxHORIZONTAL);
    bottom->Add(new wxStaticText(panel, wxID_ANY, "Search:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    _searchctrl = new wxTextCtrl(panel, WINDOW_INPUT_MODMAN_SEARCH);
    bottom->Add(_searchctrl, 1, wxEXPAND | wxALL, 5);
    bottom->Add(new wxButton(panel, WINDOW_BUTTON_MODMAN_CLOSE, "Close"), 0, wxALL, 5);
    topSizer->Add(bottom, 0, wxEXPAND);

    panel->SetSizer(topSizer);
    LoadModsFromFolder();
    RefreshLists();
}

void ModManagerFrame::LoadModsFromFolder() {
    allMods.clear();
    modMap.clear();


    for (const auto& entry : fs::directory_iterator(_modspath)) {
        if (entry.is_directory()) {
            std::string folder = entry.path().filename().string();
            std::string displayName = folder;

            try {
                fs::path metadataPath = entry.path() / "metadata.xml";
                if (fs::exists(metadataPath)) {
                    rapidxml::file<> xmlFile(metadataPath.string().c_str());
                    rapidxml::xml_document<> doc;
                    doc.parse<0>(xmlFile.data());

                    auto* metadata = doc.first_node("metadata");
                    if (metadata && metadata->first_node("name")) {
                        displayName = metadata->first_node("name")->value();
                    }
                }
            } catch (...) {
                // ignore broken xml
            }

            ModInfo info{ folder, displayName };
            allMods.push_back(info);
            modMap[folder] = info;
        }
    }
}

void ModManagerFrame::RefreshLists() {
    wxString filter = _searchctrl->GetValue().Lower();
    _enabledlist->Clear();
    _disabledlist->Clear();

    for (const ModInfo& mod : allMods) {
        wxString name = mod.displayName;
        if (!filter.IsEmpty() && !name.Lower().Contains(filter)) continue;

        if (IsDisabled(mod.folderName)) {
            _disabledlist->AppendString(name);
        } else {
            _enabledlist->AppendString(name);
        }
    }
}

bool ModManagerFrame::IsDisabled(const std::string& modFolder) {
    return fs::exists(_modspath / modFolder / "disable.it");
}

void ModManagerFrame::EnableMod(const std::string& modFolder) {
    fs::remove(_modspath / modFolder / "disable.it");
}

void ModManagerFrame::DisableMod(const std::string& modFolder) {
    std::ofstream(_modspath / modFolder / "disable.it");
}

void ModManagerFrame::OnEnableAll(wxCommandEvent&) {
    for (const ModInfo& mod : allMods) {
        EnableMod(mod.folderName);
    }
    RefreshLists();
}

void ModManagerFrame::OnDisableAll(wxCommandEvent&) {
    for (const ModInfo& mod : allMods) {
        DisableMod(mod.folderName);
    }
    RefreshLists();
}

void ModManagerFrame::OnDoubleClickEnabled(wxCommandEvent& evt) {
    wxString label = _enabledlist->GetString(evt.GetSelection());
    for (const auto& mod : allMods) {
        if (mod.displayName == label.ToStdString()) {
            DisableMod(mod.folderName);
            break;
        }
    }
    RefreshLists();
}

void ModManagerFrame::OnDoubleClickDisabled(wxCommandEvent& evt) {
    wxString label = _disabledlist->GetString(evt.GetSelection());
    for (const auto& mod : allMods) {
        if (mod.displayName == label.ToStdString()) {
            EnableMod(mod.folderName);
            break;
        }
    }
    RefreshLists();
}

void ModManagerFrame::OnSearch(wxCommandEvent&) {
    RefreshLists();
}

void ModManagerFrame::OnSave(wxCommandEvent&) {
    wxFileDialog dlg(this, "Save Enabled Mods", "", "", "Text files (*.txt)|*.txt", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK) {
        std::ofstream out(dlg.GetPath().ToStdString());
        for (const auto& mod : allMods) {
            if (!IsDisabled(mod.folderName)) {
                out << mod.folderName << "\n";
            }
        }
    }
}

void ModManagerFrame::OnLoad(wxCommandEvent&) {
    wxFileDialog dlg(this, "Load Enabled Mods", "", "", "Text files (*.txt)|*.txt", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        std::ifstream in(dlg.GetPath().ToStdString());
        std::unordered_set<std::string> enabledSet;
        std::string line;

        while (std::getline(in, line)) {
            enabledSet.insert(line);
        }

        for (const auto& mod : allMods) {
            if (enabledSet.count(mod.folderName)) {
                EnableMod(mod.folderName);
            } else {
                DisableMod(mod.folderName);
            }
        }
        RefreshLists();
    }
}

void ModManagerFrame::OnClose(wxCommandEvent&) {
    Close();
}
