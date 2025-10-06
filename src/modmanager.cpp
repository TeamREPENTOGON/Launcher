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
#include <wx/statline.h>
#include <wx/mstream.h>
#include <steam_api.h>

namespace fs = std::filesystem;

enum {
    WINDOW_BUTTON_MODMAN_ENABLEALL = wxID_HIGHEST + 1,
    WINDOW_BUTTON_MODMAN_DISABLEALL,
    WINDOW_INPUT_MODMAN_SEARCH,
    WINDOW_BUTTON_MODMAN_SAVE,
    WINDOW_BUTTON_MODMAN_LOAD,
    WINDOW_BUTTON_MODMAN_CLOSE,
    WINDOW_LIST_MODMAN_ENABLED,
    WINDOW_LIST_MODMAN_DISABLED,

    WINDOW_BUTTON_MODMAN_WORKSHOP,
    WINDOW_BUTTON_MODMAN_MODFOLDER,
    WINDOW_BUTTON_MODMAN_SAVEFOLDER
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

EVT_BUTTON(WINDOW_BUTTON_MODMAN_WORKSHOP, ModManagerFrame::OnWorkshopPage)
EVT_BUTTON(WINDOW_BUTTON_MODMAN_MODFOLDER, ModManagerFrame::OnModFolder)
EVT_BUTTON(WINDOW_BUTTON_MODMAN_SAVEFOLDER, ModManagerFrame::OnModSaveFolder)

EVT_LISTBOX(WINDOW_LIST_MODMAN_ENABLED, ModManagerFrame::OnSelectModEnabled)
EVT_LISTBOX(WINDOW_LIST_MODMAN_DISABLED, ModManagerFrame::OnSelectModDisabled)

wxEND_EVENT_TABLE()

bool issteam = false;

void OnRichTextUrlClick(wxTextUrlEvent& event)
{
    wxString url = event.GetString();
    wxLaunchDefaultBrowser(url);
}



void ModManagerFrame::OnHover(wxMouseEvent& event) {
    static wxString lastTip;
    int x = event.GetX();
    int y = event.GetY();

    long pos;
    if (extraInfoCtrl->HitTest(wxPoint(x, y), &pos) == wxRICHTEXT_HITTEST_NONE) {
        if (!lastTip.IsEmpty()) {
            extraInfoCtrl->UnsetToolTip();
            lastTip.clear();
        }
        event.Skip();
        return;
    }

    wxRichTextAttr attr;
    extraInfoCtrl->GetStyle(pos, attr);

    if (attr.HasFlag(wxTEXT_ATTR_URL) && !attr.GetURL().IsEmpty()) {
        if (lastTip != attr.GetURL()) {
            extraInfoCtrl->SetToolTip(attr.GetURL());
            lastTip = attr.GetURL();
        }
    }
    else {
        if (!lastTip.IsEmpty()) {
            extraInfoCtrl->UnsetToolTip();
            lastTip.clear();
        }
    }
    event.Skip();
}


wxImage LoadPngFromResource(HINSTANCE hInst, int resID)
{
    HRSRC hRes = FindResource(hInst, MAKEINTRESOURCE(resID), L"PNG");
    if (!hRes) {
        return wxImage();
    }
    HGLOBAL hMem = LoadResource(hInst, hRes);
    void* pData = LockResource(hMem);
    DWORD size = SizeofResource(hInst, hRes);

    if (!pData || size == 0) {
        return wxImage();
    }

    wxMemoryInputStream memStream(pData, size);
    wxImage img(memStream, wxBITMAP_TYPE_PNG);
    img.Rescale(200, 200, wxIMAGE_QUALITY_NEAREST);
    return img;
}


ModManagerFrame::ModManagerFrame(wxWindow* parent, Launcher::Installation* Instalation)
    : wxFrame(parent, wxID_ANY, "REPENTOGON Mod Manager", wxDefaultPosition, wxSize(800, 800)) {

    Center(wxBOTH);

    issteam = SteamAPI_Init();
    wxPanel* panel = new wxPanel(this);


    wxBoxSizer* leftPanel = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* thumbnbuttons = new wxBoxSizer(wxVERTICAL);
    thumbnailCtrl = new wxStaticBitmap(panel, wxID_ANY, wxBitmap(200, 200));
    thumbnbuttons->Add(thumbnailCtrl, 0, wxALL | wxALIGN_CENTER, 5);
    thumbnailCtrl->SetBitmap(wxBitmap(LoadPngFromResource(GetModuleHandle(NULL), 101)));

    thumbnbuttons->Add(new wxButton(panel, WINDOW_BUTTON_MODMAN_WORKSHOP, "Workshop Page"), 0, wxEXPAND | wxALL, 3);
    thumbnbuttons->Add(new wxButton(panel, WINDOW_BUTTON_MODMAN_MODFOLDER, "Mod Folder"), 0, wxEXPAND | wxALL, 3);
    thumbnbuttons->Add(new wxButton(panel, WINDOW_BUTTON_MODMAN_SAVEFOLDER, "Mod Save Data"), 0, wxEXPAND | wxALL, 3);
    leftPanel->Add(thumbnbuttons, 0, wxALL | wxALIGN_CENTER, 0);


    wxBoxSizer* descnextra = new wxBoxSizer(wxVERTICAL);
    selectedModTitle = new wxStaticText(panel, wxEXPAND | wxALL, "No Mod Selected 2: The Non-selectioning");
    wxFont font = selectedModTitle->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    selectedModTitle->SetFont(font);
    selectedModTitle->Wrap(850);
    descnextra->Add(selectedModTitle, 0, wxLEFT | wxTOP, 5);
    //descnextra->Add(new wxStaticText(panel, wxEXPAND | wxALL, "Description"), 0, wxTOP | wxLEFT, 5);
    descriptionCtrl = new wxRichTextCtrl(panel, wxID_ANY, "(No Mod Selected, that means no description to show!)", wxDefaultPosition, wxSize(1850, 200), wxTE_MULTILINE | wxTE_READONLY | wxTE_AUTO_URL);
    descriptionCtrl->Bind(wxEVT_TEXT_URL, &OnRichTextUrlClick);

    descnextra->Add(descriptionCtrl, 0, wxEXPAND | wxALL, 5);

    descnextra->Add(new wxStaticText(panel, wxID_ANY, "Mod Contents:"), 0, wxLEFT, 5);
    extraInfoCtrl = new wxRichTextCtrl(panel, wxEXPAND | wxALL, "Extra info", wxDefaultPosition, wxSize(1850, 50), wxTE_MULTILINE | wxTE_READONLY);
    extraInfoCtrl->Bind(wxEVT_MOTION, &ModManagerFrame::OnHover, this);

    descnextra->Add(extraInfoCtrl, 0, wxEXPAND | wxALL, 5);
    leftPanel->Add(descnextra, 0, wxLEFT | wxALIGN_CENTER, 0);

    wxBoxSizer* rightPanel = new wxBoxSizer(wxVERTICAL);
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

    rightPanel->Add(listSizer, 1, wxEXPAND);

    wxBoxSizer* bottom = new wxBoxSizer(wxHORIZONTAL);
    bottom->Add(new wxStaticText(panel, wxID_ANY, "Search:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    _searchctrl = new wxTextCtrl(panel, WINDOW_INPUT_MODMAN_SEARCH);
    bottom->Add(_searchctrl, 1, wxEXPAND | wxALL, 5);
    bottom->Add(new wxButton(panel, WINDOW_BUTTON_MODMAN_CLOSE, "Close"), 0, wxALL, 5);
    rightPanel->Add(bottom, 0, wxEXPAND);

    topSizer->Add(leftPanel, 0, wxEXPAND | wxALL, 5);

    wxStaticLine* line = new wxStaticLine(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL);
    topSizer->Add(line, 0, wxEXPAND | wxTOP | wxBOTTOM, 5);

    topSizer->Add(rightPanel, 1, wxEXPAND | wxALL, 5);

    panel->SetSizer(topSizer);
    LoadModsFromFolder();
    RefreshLists();

    Bind(wxEVT_THREAD, &ModManagerFrame::OnThreadUpdate, this);
}

void ModManagerFrame::LoadModsFromFolder() {
    allMods.clear();
    modMap.clear();


    for (const auto& entry : fs::directory_iterator(_modspath)) {
        if (entry.is_directory()) {
            ModInfo info;
            info.folderName = entry.path().filename().string();
            info.displayName = info.folderName;
            info.id = info.folderName;
            info.description = info.folderName;
            info.directory = info.folderName;

            try {
                fs::path metadataPath = entry.path() / "metadata.xml";
                if (fs::exists(metadataPath)) {
                    rapidxml::file<> xmlFile(metadataPath.string().c_str());
                    rapidxml::xml_document<> doc;
                    doc.parse<0>(xmlFile.data());

                    auto* metadata = doc.first_node("metadata");
                    if (metadata) {
                        if (metadata->first_node("name")) {
                            info.displayName = metadata->first_node("name")->value();
                        }
                        if (metadata->first_node("id")) {
                            info.id = metadata->first_node("id")->value();
                        }
                        if (metadata->first_node("description")) {
                            info.description = metadata->first_node("description")->value();
                        }
                        if (metadata->first_node("directory")) {
                            info.directory = metadata->first_node("directory")->value();
                        }

                        info.islocal = info.folderName != (info.directory + "_" + info.id);
                    }
                }
            } catch (...) {
                //ignore broken xml
            }
            allMods.push_back(info);
            modMap[info.folderName] = info;
        }
    }
}

void ModManagerFrame::RefreshLists() {
    wxString filter = _searchctrl->GetValue().Lower();
    _enabledlist->Clear();
    _disabledlist->Clear();

    for (const ModInfo& mod : allMods) {
        wxString name = mod.displayName;
        if (mod.islocal) { name = "[[DEV/NoSteam]] " + name; }
        if (!filter.IsEmpty() && !name.Lower().Contains(filter)) continue;
        if (IsDisabled(mod.folderName)) {
            _disabledlist->Append(name,new ModInfo(mod));
        } else {
            _enabledlist->Append(name, new ModInfo(mod));
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
    ModInfo* mod = static_cast<ModInfo*>(_enabledlist->GetClientData(evt.GetSelection()));
    DisableMod(mod->folderName);
    RefreshLists();
}

void ModManagerFrame::OnDoubleClickDisabled(wxCommandEvent& evt) {
    ModInfo* mod = static_cast<ModInfo*>(_disabledlist->GetClientData(evt.GetSelection()));
    EnableMod(mod->folderName);
    RefreshLists();
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    s->append((char*)contents, newLength);
    return newLength;
}

std::string GetThumbnailURL(const std::string& itemId) {
    std::string url = "https://steamcommunity.com/sharedfiles/filedetails/?id=" + itemId;
    std::string html;

    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        curl::SetupProxyForCurl(curl);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }

    std::regex imgRegex(
        R"delim(<img\s+[^>]*id="previewImageMain"[^>]*src="([^"]+)")delim",
        std::regex::icase
    );

    std::smatch match;
    if (std::regex_search(html, match, imgRegex)) {
        return match[1];
    }

    std::regex imgRegex2(
        R"delim(<img\s+[^>]*id="previewImage"[^>]*src="([^"]+)")delim",
        std::regex::icase
    );

    std::smatch match2;
    if (std::regex_search(html, match2, imgRegex2)) {
        return match2[1].str() + "&letterbox=false";
    }



    return "";
}

bool DownloadThumbFromID(const std::string& itemId, const std::string& filePath) {

    if (itemId.length() <= 0) { return false; }
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    FILE* fp = fopen(filePath.c_str(), "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, GetThumbnailURL(itemId).c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr); // Default write function
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl::SetupProxyForCurl(curl);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if (response_code != 200) {
            fclose(fp);
            remove(filePath.c_str());
            return false;
        }
    }
    else {
        fclose(fp);
        remove(filePath.c_str());
        return false;
    }

    fclose(fp);
    return res == CURLE_OK;
}

//God Bless ChatGPT for this hacky stink, FUCK YOU libpng, YOU FUCKING SUCK, "fix your png" they say....dude, its not my effing PNG! (Yes, I know this function FUCKING SUCKS, but nothing to do)
bool LoadImageQuietly(const std::string& path, wxImage& img) {
    wxLogNull noLog;

    fflush(stderr);
    int originalStderrFd = dup(fileno(stderr));
    if (originalStderrFd == -1) {
        return img.LoadFile(path, wxBITMAP_TYPE_ANY) && img.IsOk();
    }
#ifdef _WIN32
    FILE* nullFile = freopen("NUL", "w", stderr);
#else
    FILE* nullFile = freopen("/dev/null", "w", stderr);
#endif
    if (!nullFile) {
        dup2(originalStderrFd, fileno(stderr));
        close(originalStderrFd);
        return img.LoadFile(path, wxBITMAP_TYPE_ANY) && img.IsOk();
    }
    bool loaded = img.LoadFile(path, wxBITMAP_TYPE_ANY) && img.IsOk();
    fflush(stderr);
    dup2(originalStderrFd, fileno(stderr));
    close(originalStderrFd);

    return loaded;
}

bool HasChildNodesFromXML(fs::path xml, const std::string& daddy, const std::string& child = "") {
    int count = 0;
    try {
    if (fs::exists(xml)) {
        rapidxml::file<> xmlFile(xml.string().c_str());
        rapidxml::xml_document<> doc;
        doc.parse<0>(xmlFile.data());

        auto* root = doc.first_node(daddy.c_str());
        if (root) {
            if (child.length() > 0) {
                for (rapidxml::xml_node<>* babee = root->first_node(child.c_str());
                    babee;
                    babee = babee->next_sibling(child.c_str()))
                {
                    return true; //++count;
                }
            }
            else {
                for (rapidxml::xml_node<>* babee = root->first_node();
                    babee;
                    babee = babee->next_sibling())
                {
                    return true; //++count;
                }
            }
        }
    }
    }
    catch (...) {}
    return false; //return count;
}


bool HasAnm2File(fs::path folder)
{
    if (!fs::exists(folder) || !fs::is_directory(folder))
        return false;
    for (const auto& entry : fs::recursive_directory_iterator(folder))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".anm2")
            return true;
    }
    return false;
}

bool HasFile(fs::path folder, std::string ext)
{
    if (!fs::exists(folder) || !fs::is_directory(folder))
        return false;
    for (const auto& entry : fs::recursive_directory_iterator(folder))
    {
        if (entry.is_regular_file() && entry.path().extension() == ext)
            return true;
    }
    return false;
}

void ModManagerFrame::LoadModExtraData() {
    extraInfoCtrl->SetValue("Adds nothing?");
    extraInfoCtrl->SetDefaultStyle(wxTextAttr());
    extraInfoCtrl->EndURL(); //no fucking clue why, but theres some oddity where clicking a link leaves this shit open somewhere in wxwidgets internal code?
    if (!selectedMod.extradata.dataset) {
        selectedMod.extradata.dataset = true;

        std::string contents[] = { "content","content-dlc3" };
        std::string resources[] = { "resources","resources-dlc3" };
        int checks = 0;
        if (fs::exists(fs::path(_modspath / selectedMod.folderName / "content-dlc3"))) {
            checks++;
        }

        for (int i = 0; i <= checks; i++) {
            selectedMod.extradata.Items        = selectedMod.extradata.Items      || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / contents[i] / "items.xml"), "items","active") || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / "content" / "items.xml"), "items", "passive");
            selectedMod.extradata.Trinkets     = selectedMod.extradata.Trinkets   || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / contents[i] / "items.xml"), "items","trinket");
            selectedMod.extradata.Characters   = selectedMod.extradata.Characters || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / contents[i] / "players.xml"), "players");
            selectedMod.extradata.Music        = selectedMod.extradata.Music      || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / contents[i] / "music.xml"), "music","track");
            selectedMod.extradata.Sounds       = selectedMod.extradata.Sounds     || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / contents[i] / "sounds.xml"), "sounds","sound");
            selectedMod.extradata.ItemPools    = selectedMod.extradata.ItemPools  || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / contents[i] / "itempools.xml"), "itempools","pool");
            selectedMod.extradata.Shaders      = selectedMod.extradata.Shaders    || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / contents[i] / "shaders.xml"), "shaders","shader");
            selectedMod.extradata.Challenges   = selectedMod.extradata.Challenges || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / contents[i] / "challenges.xml"), "challenges");
            selectedMod.extradata.Cards        = selectedMod.extradata.Cards      || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / contents[i] / "pocketitems.xml"), "pocketitems","card");
            selectedMod.extradata.Pills        = selectedMod.extradata.Pills      || HasChildNodesFromXML(fs::path(_modspath / selectedMod.folderName / contents[i] / "pocketitems.xml"), "pocketitems","pill");
        }
        selectedMod.extradata.lua = fs::exists(fs::path(_modspath / selectedMod.folderName / "main.lua"));
        
        checks = 0;
        if (fs::exists(fs::path(_modspath / selectedMod.folderName / "resources-dlc3"))) {
            checks++;
        }
        for (int i = 0; i <= checks; i++) {
            selectedMod.extradata.resourceItems        = selectedMod.extradata.resourceItems      || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "items.xml"));
            selectedMod.extradata.resourceTrinkets     = selectedMod.extradata.resourceTrinkets   || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "items.xml"));
            selectedMod.extradata.resourceCharacters   = selectedMod.extradata.resourceCharacters || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "players.xml"));
            selectedMod.extradata.resourceMusic        = selectedMod.extradata.resourceMusic      || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "music.xml")) ;
            selectedMod.extradata.resourceSounds       = selectedMod.extradata.resourceSounds     || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "sounds.xml"));
            selectedMod.extradata.resourceItemPools    = selectedMod.extradata.resourceItemPools  || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "itempools.xml"));
            selectedMod.extradata.resourceShaders      = selectedMod.extradata.resourceShaders    || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "shaders.xml"));
            selectedMod.extradata.resourceChallenges   = selectedMod.extradata.resourceChallenges || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "challenges.xml"));
            selectedMod.extradata.resourceCards        = selectedMod.extradata.resourceCards      || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "pocketitems.xml"));
            selectedMod.extradata.resourcePills        = selectedMod.extradata.resourcePills      || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "pocketitems.xml"));
            selectedMod.extradata.anm2                 = selectedMod.extradata.anm2               || HasAnm2File(fs::path(_modspath / selectedMod.folderName / resources[i] / "gfx")); 
            selectedMod.extradata.sprites              = selectedMod.extradata.sprites            || HasFile(fs::path(_modspath / selectedMod.folderName / resources[i] / "gfx"), ".png");
            selectedMod.extradata.resourceMinor        = selectedMod.extradata.resourceMinor      || HasFile(fs::path(_modspath / selectedMod.folderName / resources[i]), ".xml");

            selectedMod.extradata.cutscenes        = selectedMod.extradata.cutscenes || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "gfx" / "cutscenes"));
            selectedMod.extradata.Sounds = selectedMod.extradata.Sounds || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "sfx"));
            selectedMod.extradata.Music = selectedMod.extradata.Music || fs::exists(fs::path(_modspath / selectedMod.folderName / resources[i] / "music"));
        }
    }
    extraInfoCtrl->SetValue("");
    wxTextAttr style;
    style.SetFontWeight(wxFONTWEIGHT_BOLD);
    style.SetTextColour(wxColor("White"));


    if (selectedMod.extradata.resourceItems || selectedMod.extradata.resourceCharacters || selectedMod.extradata.resourceMusic || selectedMod.extradata.resourceSounds || selectedMod.extradata.resourceItemPools || selectedMod.extradata.resourceShaders || selectedMod.extradata.resourceChallenges || selectedMod.extradata.resourceCards) {
        style.SetBackgroundColour(wxColor("Red")); //Blood Red of Heck
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->BeginURL("This means potential compat issues! (usually done by mistake)");
        extraInfoCtrl->WriteText(wxString::FromUTF8("<!!XML REPLACEMENTS!!>"));
        extraInfoCtrl->EndStyle();
        extraInfoCtrl->EndURL();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }
    else if (selectedMod.extradata.resourceMinor){
        style.SetBackgroundColour(wxColor("Grey")); 
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->BeginURL("Not too bad on this case, but could still be bad for compat");
        extraInfoCtrl->WriteText(wxString::FromUTF8("<XML REPLACEMENTS>"));
        extraInfoCtrl->EndStyle();
        extraInfoCtrl->EndURL();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }

    if (selectedMod.extradata.lua) {
        style.SetBackgroundColour(wxColor(0, 178, 255)); //cyanish
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<LuaCode>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }
    if (selectedMod.extradata.anm2) {
        style.SetBackgroundColour(wxColor(32, 23, 224)); //bluish
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<ANM2>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }
    if (selectedMod.extradata.sprites) {
        style.SetBackgroundColour(wxColor(40, 219, 140)); //cyanish
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<Sprites>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }
    if (selectedMod.extradata.Shaders) {
        style.SetBackgroundColour(wxColor(209, 59, 199)); //peenk
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<Shaders>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }

    if (selectedMod.extradata.Items > 0) { 
        style.SetBackgroundColour(wxColor(82, 235, 5)); //barf green
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<Items>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }
    if (selectedMod.extradata.Trinkets > 0) {
        style.SetBackgroundColour(wxColor(255, 207, 2)); //GOLD
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<Trinkets>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }

    if (selectedMod.extradata.Cards) {
        style.SetBackgroundColour(wxColor(168, 70, 1)); //Bownie
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<Cards>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }
    if (selectedMod.extradata.Pills) {
        style.SetBackgroundColour(wxColor(114, 147, 255)); //Bluey
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<Pills>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }
    if (selectedMod.extradata.Characters) {
        style.SetBackgroundColour(wxColor(242, 152, 131)); //white dude after a sunny day
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<Charas>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }
    if (selectedMod.extradata.Music) {
        style.SetBackgroundColour(wxColor(173, 101, 240)); //parpel
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<Music>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }
    if (selectedMod.extradata.Sounds) {
        style.SetBackgroundColour(wxColor(200, 207, 2)); //infected pee
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<Sounds>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }
    
    if (selectedMod.extradata.cutscenes) {
        style.SetBackgroundColour(wxColor(136, 42, 245)); //the man who slaughers or some shit, I didnt play fnaf!
        extraInfoCtrl->BeginStyle(style);
        extraInfoCtrl->WriteText(wxString::FromUTF8("<Cutscenes>"));
        extraInfoCtrl->EndStyle();
        style.SetBackgroundColour(wxColor("White"));
        extraInfoCtrl->WriteText(wxString::FromUTF8(" "));
    }


}


void ParseBBCode(wxRichTextCtrl* field, const std::string& bbcode) {
    field->SetValue("");
    field->SetDefaultStyle(wxTextAttr());
    field->EndURL(); //no fucking clue why, but theres some oddity where clicking a link leaves this shit open somewhere in wxwidgets internal code?
    size_t pos = 0;
    wxTextAttr style;


    auto writeTextWithUrls = [&](const std::string& text) {
        static const std::regex url_regex(R"((https?:\/\/[^\s\]]+))", std::regex::icase);
        std::sregex_iterator iter(text.begin(), text.end(), url_regex);
        std::sregex_iterator end;

        size_t last_pos = 0;
        for (; iter != end; ++iter) {
            if (iter->position() > last_pos) {
                field->BeginStyle(style);
                field->WriteText(wxString::FromUTF8(text.substr(last_pos, iter->position() - last_pos)));
                field->EndStyle();
            }
            std::string url = iter->str();
            style.SetTextColour("Blue"); //not the exact URL-color but fuck it, close enough
            field->BeginStyle(style);
            field->BeginURL(url);
            field->WriteText(wxString::FromUTF8(url));
            field->EndURL();
            field->EndStyle();
            style.SetTextColour("Black");
            last_pos = iter->position() + iter->length();
        }
        if (last_pos < text.size()) {
            field->BeginStyle(style);
            field->WriteText(wxString::FromUTF8(text.substr(last_pos)));
            field->EndStyle();
        }
    };

    while (pos < bbcode.size()) {
        size_t tag_start = bbcode.find('[', pos);
        if (tag_start == std::string::npos) {
            writeTextWithUrls(bbcode.substr(pos));
            break;
        }

        if (tag_start > pos) {
            writeTextWithUrls(bbcode.substr(pos, tag_start - pos));
        }

        size_t tag_end = bbcode.find(']', tag_start);
        if (tag_end == std::string::npos) break;

        std::string tag = bbcode.substr(tag_start + 1, tag_end - tag_start - 1);

        if (tag == "b") {
            style.SetFontWeight(wxFONTWEIGHT_BOLD);
            pos = tag_end + 1;
        }
        else if (tag == "/b") {
            style.SetFontWeight(wxFONTWEIGHT_NORMAL);
            pos = tag_end + 1;
        }
        else if (tag == "i") {
            style.SetFontStyle(wxFONTSTYLE_ITALIC);
            pos = tag_end + 1;
        }
        else if (tag == "/i") {
            style.SetFontStyle(wxFONTSTYLE_NORMAL);
            pos = tag_end + 1;
        }
        else if (tag == "u") {
            style.SetFontUnderlined(true);
            pos = tag_end + 1;
        }
        else if (tag == "/u") {
            style.SetFontUnderlined(false);
            pos = tag_end + 1;
        }
        else if (tag == "strike") {
            style.SetFontStrikethrough(true);
            pos = tag_end + 1;
        }
        else if (tag == "/strike") {
            style.SetFontStrikethrough(false);
            pos = tag_end + 1;
        }
        else if (tag == "spoiler") {
            style.SetBackgroundColour(wxColor("Black"));
            pos = tag_end + 1;
        }
        else if (tag == "/spoiler") {
            style.SetBackgroundColour(wxColor("White"));
            pos = tag_end + 1;
        }
        else if (tag == "*") {
            field->WriteText("* ");
            pos = tag_end + 1;
        }
        else if (tag.compare(0, 6, "color=") == 0) {
            std::string color_name = tag.substr(6);
            wxColour col(color_name);
            style.SetTextColour(col);
            pos = tag_end + 1;
        }
        else if (tag == "/color") {
            style.SetTextColour(*wxBLACK);
            pos = tag_end + 1;
        }
        else if (tag.compare(0, 2, "h1") == 0) {
            style.SetFontWeight(wxFONTWEIGHT_BOLD);
            style.SetFontSize(13);
            pos = tag_end + 1;
        }
        else if (tag == "/h1") {
            style.SetFontWeight(wxFONTWEIGHT_NORMAL);
            style.SetFontSize(10);
            pos = tag_end + 1;
        }
        else if (tag.compare(0, 2, "h2") == 0) {
            style.SetFontWeight(wxFONTWEIGHT_BOLD);
            style.SetFontSize(12);
            pos = tag_end + 1;
        }
        else if (tag == "/h2") {
            style.SetFontWeight(wxFONTWEIGHT_NORMAL);
            style.SetFontSize(10);
            pos = tag_end + 1;
        }
        else if (tag.compare(0, 2, "h3") == 0) {
            style.SetFontWeight(wxFONTWEIGHT_BOLD);
            style.SetFontSize(11);
            pos = tag_end + 1;
        }
        else if (tag == "/h3") {
            style.SetFontWeight(wxFONTWEIGHT_NORMAL);
            style.SetFontSize(10);
            pos = tag_end + 1;
        }
        else {
            pos = tag_end + 1;
        }
    }
}

void ModManagerFrame::OnSelectModEnabled(wxCommandEvent& evt) {
    ModInfo* mod = static_cast<ModInfo*>(_enabledlist->GetClientData(evt.GetSelection()));
    ModManagerFrame::OnSelectMod(*mod);
}

void ModManagerFrame::OnSelectModDisabled(wxCommandEvent& evt) {
    ModInfo* mod = static_cast<ModInfo*>(_disabledlist->GetClientData(evt.GetSelection()));
    ModManagerFrame::OnSelectMod(*mod);
}


void ModManagerFrame::OnSelectMod(ModInfo mod) {
            ParseBBCode(descriptionCtrl, mod.description.empty() ? "(No description)" : mod.description);

            fs::path imagePath = _modspath / mod.folderName / "thumb.png";
            if (fs::exists(imagePath) && fs::file_size(imagePath) > 0) {
                wxImage img;
                if (LoadImageQuietly(imagePath.string(), img)) {
                    img.Rescale(200, 200, wxIMAGE_QUALITY_HIGH);
                    thumbnailCtrl->SetBitmap(wxBitmap(img));
                }
                else {
                    thumbnailCtrl->SetBitmap(wxBitmap(200, 200));
                }
            }
            else {
                thumbnailCtrl->SetBitmap(wxBitmap(200, 200));
                if ((mod.id.length() > 0) && (mod.id != mod.folderName)) { //check if the thing actually has a valid id
                    thumbnailCtrl->SetBitmap(wxBitmap(LoadPngFromResource(GetModuleHandle(NULL), 102)));
                    std::thread([this, id = mod.id, imagePath]() {
                    if (DownloadThumbFromID(id, imagePath.string())) {
                        try {
                        wxImage img;
                        if (LoadImageQuietly(imagePath.string(), img)) {
                            img.Rescale(200, 200, wxIMAGE_QUALITY_HIGH);
                                if (selectedMod.id == id) { //so it doesnt get replaced if the mod changes while other thumbs are loading
                                    wxThreadEvent* evt = new wxThreadEvent(wxEVT_THREAD);
                                    evt->SetPayload<wxBitmap>(wxBitmap(img));
                                    if (!IsBeingDeleted()) {
                                        wxQueueEvent(this, evt);
                                    }
                                }
                                
                        }
                        }
                        catch (...) {} //dont give a shit, this is not critical at all
                    }
                    else {
                        wxThreadEvent* evt = new wxThreadEvent(wxEVT_THREAD);
                        evt->SetPayload<wxBitmap>(wxBitmap(LoadPngFromResource(GetModuleHandle(NULL), 101)));
                        if (!IsBeingDeleted()) {
                            wxQueueEvent(this, evt);
                        }
                    }
                    }).detach();
                }
                else {
                    thumbnailCtrl->SetBitmap(wxBitmap(LoadPngFromResource(GetModuleHandle(NULL), 101)));
                }
            }
            selectedModTitle->SetLabel(mod.displayName);
            selectedMod = mod;
            LoadModExtraData();
}

void ModManagerFrame::OnWorkshopPage(wxCommandEvent&) {
    if (!selectedMod.id.empty()) {
        wxLaunchDefaultBrowser("https://steamcommunity.com/sharedfiles/filedetails/?id=" + selectedMod.id);
    }
}

void ModManagerFrame::OnModFolder(wxCommandEvent&) {
    if (!selectedMod.folderName.empty()) {
        wxLaunchDefaultBrowser("file:///" + fs::absolute(_modspath / selectedMod.folderName).string());
    }
}

void ModManagerFrame::OnModSaveFolder(wxCommandEvent&) {
    fs::path savepath = fs::absolute(_modspath.parent_path() / "data" / selectedMod.directory);
    if (!fs::exists(savepath)) {
        fs::create_directories(savepath); //Why?, because theres the case where someone may want to share a savefile with some dude that has none
    }

    if (!selectedMod.displayName.empty()) {
        wxLaunchDefaultBrowser("file:///" + savepath.string());
    }
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
