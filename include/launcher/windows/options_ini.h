#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>
#include <wx/valnum.h>
#include <wx/valgen.h>

class GameOptions {
public:
    std::string filepath = "options.ini";

    int Language = 0; 
    float MusicVolume = 0.0f;
    int MusicEnabled = 0;
    float SFXVolume = 0.0f;
    float MapOpacity = 0.0f;
    int Fullscreen = 0;
    int Filter = 0;
    float Exposure = 1.0f;
    float Gamma = 1.0f;
    int ControllerHotplug = 0;
    int PopUps = 0;
    int CameraStyle = 0;
    int ShowRecentItems = 0;
    float HudOffset = 0.0f;
    int TryImportSave = 0;
    int FoundHUD = 0;
    int EnableMods = 0;
    int RumbleEnabled = 0;
    int ChargeBars = 0;
    int BulletVisibility = 0;
    int TouchMode = 0; //super pointless not even displaying it
    int AimLock = 0;
    int JacobEsauControls = 0;
    int AscentVoiceOver = 0;
    int OnlineHud = 0;
    int StreamerMode = 0;
    int OnlinePlayerVolume = 0;
    int OnlinePlayerOpacity = 0;
    int OnlineChatEnabled = 0;
    int OnlineChatFilterEnabled = 0;
    int MultiplayerColorSet = 0;
    int OnlineInputDelay = 0; 
    int AcceptedModDisclaimer = 0; //pointless
    int AcceptedDataCollectionDisclaimer = 0; //pointless
    int EnableDebugConsole = 0;
    int MaxScale = 0;
    int MaxRenderScale = 0;
    int VSync = 0;
    int PauseOnFocusLost = 0;
    int SteamCloud = 0;
    int MouseControl = 0;
    int BossHpOnBottom = 0;
    int AnnouncerVoiceMode = 0;
    int ConsoleFont = -1;
    int FadedConsoleDisplay = 0;
    int SaveCommandHistory = 0;
    int WindowWidth = 0;
    int WindowHeight = 0;
    int WindowPosX = 0;
    int WindowPosY = 0;
    std::unordered_map<std::string, std::string> unsupportedoptions;

    bool Load(const std::string& filename) {
        filepath = filename;
        std::ifstream file(filename);
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '[') continue; // skip headers and blank lines
            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key = line.substr(0, eqPos);
            std::string val = line.substr(eqPos + 1);

            SetOption(key, val);
        }
        return true;
    }

    bool Save() const {
        std::ofstream file(filepath);
        if (!file.is_open()) return false;

    file << "[Options]\n";
    file << "Language=" << Language << "\n";
    file << "MusicVolume=" << std::fixed << std::setprecision(4) << MusicVolume << "\n";
    file << "MusicEnabled=" << MusicEnabled << "\n";
    file << "SFXVolume=" << std::fixed << std::setprecision(4) << SFXVolume << "\n";
    file << "MapOpacity=" << std::fixed << std::setprecision(4) << MapOpacity << "\n";
    file << "Fullscreen=" << Fullscreen << "\n";
    file << "Filter=" << Filter << "\n";
    file << "Exposure=" << std::fixed << std::setprecision(4) << Exposure << "\n";
    file << "Gamma=" << std::fixed << std::setprecision(4) << Gamma << "\n";
    file << "ControllerHotplug=" << ControllerHotplug << "\n";
    file << "PopUps=" << PopUps << "\n";
    file << "CameraStyle=" << CameraStyle << "\n";
    file << "ShowRecentItems=" << ShowRecentItems << "\n";
    file << "HudOffset=" << std::fixed << std::setprecision(4) << HudOffset << "\n";
    file << "TryImportSave=" << TryImportSave << "\n";
    file << "FoundHUD=" << FoundHUD << "\n";
    file << "EnableMods=" << EnableMods << "\n";
    file << "RumbleEnabled=" << RumbleEnabled << "\n";
    file << "ChargeBars=" << ChargeBars << "\n";
    file << "BulletVisibility=" << BulletVisibility << "\n";
    file << "TouchMode=" << TouchMode << "\n";
    file << "AimLock=" << AimLock << "\n";
    file << "JacobEsauControls=" << JacobEsauControls << "\n";
    file << "AscentVoiceOver=" << AscentVoiceOver << "\n";
    file << "OnlineHud=" << OnlineHud << "\n";
    file << "StreamerMode=" << StreamerMode << "\n";
    file << "OnlinePlayerVolume=" << OnlinePlayerVolume << "\n";
    file << "OnlinePlayerOpacity=" << OnlinePlayerOpacity << "\n";
    file << "OnlineChatEnabled=" << OnlineChatEnabled << "\n";
    file << "OnlineChatFilterEnabled=" << OnlineChatFilterEnabled << "\n";
    file << "MultiplayerColorSet=" << MultiplayerColorSet << "\n";
    file << "OnlineInputDelay=" << OnlineInputDelay << "\n";
    file << "AcceptedModDisclaimer=" << AcceptedModDisclaimer << "\n";
    file << "AcceptedDataCollectionDisclaimer=" << AcceptedDataCollectionDisclaimer << "\n";
    file << "EnableDebugConsole=" << EnableDebugConsole << "\n";
    file << "MaxScale=" << MaxScale << "\n";
    file << "MaxRenderScale=" << MaxRenderScale << "\n";
    file << "VSync=" << VSync << "\n";
    file << "PauseOnFocusLost=" << PauseOnFocusLost << "\n";
    file << "SteamCloud=" << SteamCloud << "\n";
    file << "MouseControl=" << MouseControl << "\n";
    file << "BossHpOnBottom=" << BossHpOnBottom << "\n";
    file << "AnnouncerVoiceMode=" << AnnouncerVoiceMode << "\n";
    file << "ConsoleFont=" << ConsoleFont << "\n";
    file << "FadedConsoleDisplay=" << FadedConsoleDisplay << "\n";
    file << "SaveCommandHistory=" << SaveCommandHistory << "\n";
    file << "WindowWidth=" << WindowWidth << "\n";
    file << "WindowHeight=" << WindowHeight << "\n";
    file << "WindowPosX=" << WindowPosX << "\n";
    file << "WindowPosY=" << WindowPosY << "\n";

    for (auto& entry : unsupportedoptions) {
        file << entry.first << "=" << entry.second << "\n";
    }

        return true;
    }

private:
    void SetOption(const std::string& key, const std::string& val) {
        std::istringstream ss(val);


        if (key == "Language") ss >> Language; //could make an elaborated structure but fuck it, a script did it, not me!....also got tricky for a structure because of fucking types...
        else if (key == "MusicVolume") ss >> MusicVolume;
        else if (key == "MusicEnabled") ss >> MusicEnabled;
        else if (key == "SFXVolume") ss >> SFXVolume;
        else if (key == "MapOpacity") ss >> MapOpacity;
        else if (key == "Fullscreen") ss >> Fullscreen;
        else if (key == "Filter") ss >> Filter;
        else if (key == "Exposure") ss >> Exposure;
        else if (key == "Gamma") ss >> Gamma;
        else if (key == "ControllerHotplug") ss >> ControllerHotplug;
        else if (key == "PopUps") ss >> PopUps;
        else if (key == "CameraStyle") ss >> CameraStyle;
        else if (key == "ShowRecentItems") ss >> ShowRecentItems;
        else if (key == "HudOffset") ss >> HudOffset;
        else if (key == "TryImportSave") ss >> TryImportSave;
        else if (key == "FoundHUD") ss >> FoundHUD;
        else if (key == "EnableMods") ss >> EnableMods;
        else if (key == "RumbleEnabled") ss >> RumbleEnabled;
        else if (key == "ChargeBars") ss >> ChargeBars;
        else if (key == "BulletVisibility") ss >> BulletVisibility;
        else if (key == "TouchMode") ss >> TouchMode;
        else if (key == "AimLock") ss >> AimLock;
        else if (key == "JacobEsauControls") ss >> JacobEsauControls;
        else if (key == "AscentVoiceOver") ss >> AscentVoiceOver;
        else if (key == "OnlineHud") ss >> OnlineHud;
        else if (key == "StreamerMode") ss >> StreamerMode;
        else if (key == "OnlinePlayerVolume") ss >> OnlinePlayerVolume;
        else if (key == "OnlinePlayerOpacity") ss >> OnlinePlayerOpacity;
        else if (key == "OnlineChatEnabled") ss >> OnlineChatEnabled;
        else if (key == "OnlineChatFilterEnabled") ss >> OnlineChatFilterEnabled;
        else if (key == "MultiplayerColorSet") ss >> MultiplayerColorSet;
        else if (key == "OnlineInputDelay") ss >> OnlineInputDelay;
        else if (key == "AcceptedModDisclaimer") ss >> AcceptedModDisclaimer;
        else if (key == "AcceptedDataCollectionDisclaimer") ss >> AcceptedDataCollectionDisclaimer;
        else if (key == "EnableDebugConsole") ss >> EnableDebugConsole;
        else if (key == "MaxScale") ss >> MaxScale;
        else if (key == "MaxRenderScale") ss >> MaxRenderScale;
        else if (key == "VSync") ss >> VSync;
        else if (key == "PauseOnFocusLost") ss >> PauseOnFocusLost;
        else if (key == "SteamCloud") ss >> SteamCloud;
        else if (key == "MouseControl") ss >> MouseControl;
        else if (key == "BossHpOnBottom") ss >> BossHpOnBottom;
        else if (key == "AnnouncerVoiceMode") ss >> AnnouncerVoiceMode;
        else if (key == "ConsoleFont") ss >> ConsoleFont;
        else if (key == "FadedConsoleDisplay") ss >> FadedConsoleDisplay;
        else if (key == "SaveCommandHistory") ss >> SaveCommandHistory;
        else if (key == "WindowWidth") ss >> WindowWidth;
        else if (key == "WindowHeight") ss >> WindowHeight;
        else if (key == "WindowPosX") ss >> WindowPosX;
        else if (key == "WindowPosY") ss >> WindowPosY;
        else unsupportedoptions[key] = val;
    }
};



class OptionsDialog : public wxDialog {
public:
    OptionsDialog(wxWindow* parent, GameOptions& opts)
        : wxDialog(parent, wxID_ANY, "Game Options", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          optionss(opts)
    {
        wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

        wxStaticBoxSizer* optionsBox = new wxStaticBoxSizer(wxHORIZONTAL, this, "Options");;
        wxBoxSizer* ROW1 = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* ROW2 = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* ROW3 = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* ROW4 = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* ROW5 = new wxBoxSizer(wxVERTICAL);
        

        AddCheckBox(ROW1, "Enable Mods", &optionss.EnableMods, enableModsCheck);
        AddCheckBox(ROW1, "Filter", &optionss.Filter, filterCheck);
        AddCheckBox(ROW1, "Fullscreen", &optionss.Fullscreen, fullscreenCheck);
        AddCheckBox(ROW1, "Vertical Sync", &optionss.VSync, vsyncChk);
        AddCheckBox(ROW1, "Controller Hotplug", &optionss.ControllerHotplug, hotplugCheck);
        AddCheckBox(ROW1, "Rumble Enabled", &optionss.RumbleEnabled, rumbleEnabledCheck);
        AddCheckBox(ROW1, "Mouse Control", &optionss.MouseControl, mouseControlChk);
        AddCheckBox(ROW1, "Music Enabled", &optionss.MusicEnabled, musicEnabledChk);
        AddCheckBox(ROW1, "Chargebars", &optionss.ChargeBars, chargebarsChk);
        AddCheckBox(ROW1, "Bullet Visibility", &optionss.BulletVisibility, bulletVisibilityChk);
        AddCheckBox(ROW1, "Pause on Focus Lost", &optionss.PauseOnFocusLost, pauseOnFocusChk);
        AddCheckBox(ROW1, "Try Import Save", &optionss.TryImportSave, tryImportSaveChk);

        AddCheckBox(ROW2, "Save Command History", &optionss.SaveCommandHistory, saveHistoryChk);
        AddCheckBox(ROW2, "Debug Console", &optionss.EnableDebugConsole, enableDebugConsoleChk);
        AddCheckBox(ROW2, "Faded Console Display", &optionss.FadedConsoleDisplay, fadedConsoleChk);
        AddCheckBox(ROW2, "Active Cam", &optionss.CameraStyle, cameraStyleChk);
        AddCheckBox(ROW2, "Found HUD", &optionss.FoundHUD, foundHudChk);
        AddCheckBox(ROW2, "Aim Lock", &optionss.AimLock, aimLockChk);
        AddCheckBox(ROW2, "Ascent VoiceOver", &optionss.AscentVoiceOver, ascentVoiceChk);
        AddCheckBox(ROW2, "Steam Cloud", &optionss.SteamCloud, steamCloudChk);
        AddCheckBox(ROW2, "Boss HP On Bottom", &optionss.BossHpOnBottom, bossHpOnBottomChk);
        AddCheckBox(ROW2, "Streamer Mode", &optionss.StreamerMode, streamerModeChk);
        AddCheckBox(ROW2, "Online Chat Enabled", &optionss.OnlineChatEnabled, onlineChatEnabledChk);
        AddCheckBox(ROW2, "Online Chat Filter", &optionss.OnlineChatFilterEnabled, onlineChatFilterChk);

        AddChoiceCtrl(ROW3, "Console Font", &optionss.ConsoleFont, consoleFontDrp, { {0,"Default"},{1,"Small"} ,{2,"Tiny"} });
        AddChoiceCtrl(ROW3, "Language", &optionss.Language, languageDrp, { {0,"English"},{2,"Japanese"},{3,"French"},{4,"Spanish"},{5,"German"},{10,"Russian"},{11,"Korean"},{13,"Chinese"} });
        AddChoiceCtrl(ROW3, "PopUps", &optionss.PopUps, popupsDrp, { {0,"Off"},{1,"Large"} ,{2,"Small"} });
        AddChoiceCtrl(ROW3, "Show Recent Items", &optionss.ShowRecentItems, showRecentItemsDrp, { {0,"Off"},{1,"Normal"} ,{2,"Mini"} });
        AddChoiceCtrl(ROW3, "JacobEsau Controls", &optionss.JacobEsauControls, jacobEsauDrp, { {0,"Classic"},{1,"Better"} });
        AddChoiceCtrl(ROW3, "Multiplayer Color Set", &optionss.MultiplayerColorSet, multiplayerColorSetDrp, { {0,"Set 1"},{1,"Set 2"},{2,"Set 3"},{3,"Set 4"} });
        AddChoiceCtrl(ROW3, "Announcer Voice Mode", &optionss.AnnouncerVoiceMode, announcerVoiceModeDrp, { {0,"Random"},{1,"Off"} ,{2,"Always on"} });
        AddChoiceCtrl(ROW3, "Online HUD", &optionss.OnlineHud, onlineHudDrp, { {0,"Normal"},{1,"Mini"},{2,"Hearts"},{3,"Off"} });

        AddFloatCtrl(ROW3, "Music Volume", &optionss.MusicVolume, musicVolumeTxt,0,1);
        AddFloatCtrl(ROW3, "SFX Volume", &optionss.SFXVolume, sfxVolumeTxt, 0, 1);
        AddFloatCtrl(ROW4, "Map Opacity", &optionss.MapOpacity,         mapOpacityTxt,0,1);
        AddFloatCtrl(ROW4, "Exposure", &optionss.Exposure,           exposureTxt,0,1);
        AddFloatCtrl(ROW4, "Gamma", &optionss.Gamma,              gammaTxt,0,2);
        AddFloatCtrl(ROW4, "Hud Offset", &optionss.HudOffset,          hudOffsetTxt, 0, 1);
        AddIntCtrl(ROW3, "Online Player Volume", &optionss.OnlinePlayerVolume,  onlinePlayerVolumeTxt,0,10);
        AddIntCtrl(ROW4, "Max Scale", &optionss.MaxScale,           maxScaleTxt);
        AddIntCtrl(ROW4, "Max Render Scale", &optionss.MaxRenderScale,     maxRenderScaleTxt);

        AddIntCtrl(ROW4, "Window Width", &optionss.WindowWidth,        windowWidthTxt);
        AddIntCtrl(ROW4, "Window Height", &optionss.WindowHeight,       windowHeightTxt);
        AddIntCtrl(ROW4, "Window Pos X", &optionss.WindowPosX,         windowPosXTxt);
        AddIntCtrl(ROW4, "Window Pos Y",       &optionss.WindowPosY,         windowPosYTxt);
        AddIntCtrl(ROW4, "Online Player Opacity", &optionss.OnlinePlayerOpacity, onlinePlayerOpacityTxt, 0, 10);
        AddIntCtrl(ROW4, "Online Input Delay", &optionss.OnlineInputDelay, onlineInputDelayTxt, 1, 5);

        optionsBox->Add(ROW1, 1, wxEXPAND | wxALL, 5);
        optionsBox->Add(ROW2, 1, wxEXPAND | wxALL, 5);
        optionsBox->Add(ROW3, 1, wxEXPAND | wxALL, 5);
        optionsBox->Add(ROW4, 1, wxEXPAND | wxALL, 5);
        optionsBox->Add(ROW5, 1, wxEXPAND | wxALL, 5);
        mainSizer->Add(optionsBox, 1, wxEXPAND | wxALL, 5);

        // --- Buttons ---
        wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
        wxButton* cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");
        wxButton* acceptBtn = new wxButton(this, wxID_OK, "Accept");
        btnSizer->AddStretchSpacer();
        btnSizer->Add(cancelBtn, 0, wxALL, 5);
        btnSizer->Add(acceptBtn, 0, wxALL, 5);
        mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 5);

        SetSizerAndFit(mainSizer);

        Bind(wxEVT_BUTTON, &OptionsDialog::OnAccept, this, wxID_OK);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ EndModal(wxID_CANCEL); }, wxID_CANCEL);

        Center();
    }

private:

    GameOptions& optionss;

    wxCheckBox* filterCheck;
    wxCheckBox* fullscreenCheck;
    wxCheckBox* hotplugCheck;
    wxChoice* popupsDrp;
    wxCheckBox* enableModsCheck;
    wxCheckBox* rumbleEnabledCheck;
    wxChoice* languageDrp;
    wxTextCtrl* musicVolumeTxt;
    wxCheckBox* musicEnabledChk;
    wxCheckBox* bulletVisibilityChk;
    wxCheckBox* chargebarsChk;
    wxTextCtrl* sfxVolumeTxt;
    wxTextCtrl* mapOpacityTxt;
    wxTextCtrl* exposureTxt;
    wxTextCtrl* gammaTxt;
    wxCheckBox* cameraStyleChk;
    wxChoice* showRecentItemsDrp;
    wxTextCtrl* hudOffsetTxt;
    wxCheckBox* tryImportSaveChk;
    wxCheckBox* foundHudChk;
    wxCheckBox* aimLockChk;
    wxChoice* jacobEsauDrp;
    wxCheckBox* ascentVoiceChk;
    wxChoice* onlineHudDrp;
    wxCheckBox* streamerModeChk;
    wxTextCtrl* onlinePlayerVolumeTxt;
    wxTextCtrl* onlinePlayerOpacityTxt;
    wxCheckBox* onlineChatEnabledChk;
    wxCheckBox* onlineChatFilterChk;
    wxChoice* multiplayerColorSetDrp;
    wxTextCtrl* onlineInputDelayTxt;
    wxCheckBox* acceptedModDisclaimerChk;
    wxCheckBox* acceptedDataDisclaimerChk;
    wxCheckBox* enableDebugConsoleChk;
    wxTextCtrl* maxScaleTxt;
    wxTextCtrl* maxRenderScaleTxt;
    wxCheckBox* vsyncChk;
    wxCheckBox* pauseOnFocusChk;
    wxCheckBox* steamCloudChk;
    wxCheckBox* mouseControlChk;
    wxCheckBox* bossHpOnBottomChk;
    wxChoice* announcerVoiceModeDrp;
    wxChoice* consoleFontDrp;
    wxCheckBox* fadedConsoleChk;
    wxCheckBox* saveHistoryChk;
    wxTextCtrl* windowWidthTxt;
    wxTextCtrl* windowHeightTxt;
    wxTextCtrl* windowPosXTxt;
    wxTextCtrl* windowPosYTxt;

    
    void AddCheckBox(wxSizer* sizer, const wxString& label, int* value, wxCheckBox*& outCtrl) {
        wxGenericValidator rules((bool*)value);

        wxStaticText* txt = new wxStaticText(this, wxID_ANY, label);
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        int width, height;
        txt->GetTextExtent("Announcer Voice Mode ", &width, &height);
        txt->SetMinSize(wxSize(width, height));

        row->Add(txt);


        outCtrl = new wxCheckBox(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0, rules);
        outCtrl->SetValue(*value != 0);
        row->Add(outCtrl, 0);
        sizer->Add(row, wxTOP, 10);
        
    }


    void AddIntCtrl(wxSizer* sizer, const wxString& label, int* value, wxTextCtrl*& outCtrl, float min = -99999, float max = 99999) {
        wxIntegerValidator<int> rules(value);
        rules.SetRange(min, max);

        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* txt = new wxStaticText(this, wxID_ANY, label);

        int width, height;
        txt->GetTextExtent("Announcer Voice Mode ", &width, &height);
        txt->SetMinSize(wxSize(width, height));
        txt->GetTextExtent("000000.000000", &width, &height);

        row->Add(txt);
        outCtrl = new wxTextCtrl(this, wxID_ANY, wxString::Format("%g", (double)*value), wxDefaultPosition, wxSize(width, wxDefaultSize.y), 0, rules);
        row->Add(outCtrl, 1);
        sizer->Add(row, wxTOP, 10);
    }

    void AddFloatCtrl(wxSizer* sizer, const wxString& label, float* value, wxTextCtrl*& outCtrl,float min = -99999, float max = 99999) {
        wxFloatingPointValidator<float> rules(4,value);
        rules.SetRange(min, max);

        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* txt = new wxStaticText(this, wxID_ANY, label);

        int width, height;
        txt->GetTextExtent("Announcer Voice Mode ", &width, &height);
        txt->SetMinSize(wxSize(width, height));
        txt->GetTextExtent("000000.000000", &width, &height);

        row->Add(txt);
        outCtrl = new wxTextCtrl(this, wxID_ANY, wxString::Format("%.4f", *value),wxDefaultPosition, wxSize(width, wxDefaultSize.y),0, rules);

        row->Add(outCtrl, 1);
        sizer->Add(row, wxTOP, 10);
    }


    void AddChoiceCtrl(wxSizer* sizer,
        const wxString& label,
        int* value,                         
        wxChoice*& outCtrl,
        const std::vector<std::pair<int, wxString>>& options)
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* txt = new wxStaticText(this, wxID_ANY, label);

        int width, height;
        txt->GetTextExtent("Announcer Voice Mode ", &width, &height);
        txt->SetMinSize(wxSize(width, height));
        txt->GetTextExtent("000000.000000", &width, &height);

        row->Add(txt);

        wxArrayString labels;
        for (auto& opt : options)
            labels.Add(opt.second);

        outCtrl = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(width, wxDefaultSize.y), labels);

        int sel = 0;
        for (size_t i = 0; i < options.size(); ++i) {
            if (options[i].first == *value) {
                sel = static_cast<int>(i);
                break;
            }
        }
        outCtrl->SetSelection(sel);

       
        outCtrl->Bind(wxEVT_CHOICE, [=](wxCommandEvent&) { //This is only needed because some shit like the Langauge option goes like 0,2,10....probably because someone at Nicalis thought it was funny or something.
            int idx = outCtrl->GetSelection();
        if (idx >= 0 && idx < (int)options.size()) {
            *value = options[idx].first;  
        }
            });

        row->Add(outCtrl, 1);
        sizer->Add(row, wxTOP, 10);
    }


    void OnAccept(wxCommandEvent&) {
        if (!TransferDataFromWindow()) return; //wxwidgets needs this shit to run to sinc th validators with the fields, its kind of ass...but, it gives an opportunity to filter out invalid inputs....by throwing an exception, lol (worst case scenario)
        optionss.Save();
        EndModal(wxID_OK);
    }

};