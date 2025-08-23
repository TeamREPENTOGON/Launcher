#pragma once

#include <wx/wx.h>
#include <wx/listbox.h>
#include <wx/textctrl.h>
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <launcher/windows/launcher.h>
#include <wx/richtext/richtextctrl.h>

namespace fs = std::filesystem;

struct ModExtraData {
    bool lua = false;
    bool anm2 = false;
    bool sprites = false;

    bool Items = false;
    bool Trinkets = false;
    bool Characters = false;
    bool Music = false;
    bool Sounds = false;
    bool Challenges = false;
    bool ItemPools = false;
    bool Cards = false;
    bool Pills = false;
    bool Shaders = false;

    bool resourceItems = false;
    bool resourceTrinkets = false;
    bool resourceCharacters = false;
    bool resourceMusic = false;
    bool resourceSounds = false;
    bool resourceChallenges = false;
    bool resourceItemPools = false;
    bool resourceCards = false;
    bool resourcePills = false;
    bool resourceShaders = false;
    bool cutscenes = false;

    bool resourceMinor = false;

    bool dataset = false;
};

struct ModInfo {
    std::string folderName;
    std::string displayName;
    std::string description;
    std::string directory;
    std::string id;
    ModExtraData extradata;
};

class ModManagerFrame : public wxFrame { //Thought of making it a dialog, but I dont think it makes that much sense here
public:
    ModManagerFrame(wxWindow* parent, Launcher::Installation* Instalation);
    void RefreshLists();

private:
    wxStaticBitmap* thumbnailCtrl;
    wxRichTextCtrl* descriptionCtrl;
    wxRichTextCtrl* extraInfoCtrl;

    ModInfo* selectedMod;
    wxStaticText* selectedModTitle;

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

    void LoadModExtraData();
    void OnSelectMod(wxCommandEvent& event);
    void OnWorkshopPage(wxCommandEvent& event);
    void OnModFolder(wxCommandEvent& event);
    void OnModSaveFolder(wxCommandEvent& event);
    void OnHover(wxMouseEvent& event);

    void EnableMod(const std::string& modfolder);
    void DisableMod(const std::string& modfolder);
    bool IsDisabled(const std::string& modfolder);

    wxDECLARE_EVENT_TABLE();
};
