#include <wx/wx.h>
#include <wx/thread.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <atomic>
#include <thread>
#include <algorithm>
#include <functional>
#include "steam_api.h"
#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_utils.hpp"
#include <unordered_set>
#include "widgets/text_ctrl_log_widget.h"

namespace fs = std::filesystem;



class ModUpdateDialog : public wxDialog {
public:
    wxTextCtrlLog* launcherlogger;

    ModUpdateDialog(wxWindow* parent,
        const fs::path& targetModsDir,wxTextCtrlLog* logger)
        : wxDialog(parent, wxID_ANY, "Copying Mod files from Steam...",
            wxDefaultPosition, wxSize(600, 300)),
        targetModsDir(targetModsDir), cancelrequest(false)
    {

        launcherlogger = logger;
        wxBoxSizer* v = new wxBoxSizer(wxVERTICAL);
        statuslog = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 180));
        v->Add(statuslog, 1, wxEXPAND | wxALL, 8);

        progresstxt = new wxStaticText(this, wxID_ANY, "Processed 0 / 0");
        v->Add(progresstxt, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        loadbar = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 24));
        v->Add(loadbar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        wxBoxSizer* h = new wxBoxSizer(wxHORIZONTAL);
        cancelbtn = new wxButton(this, wxID_CANCEL, "Cancel");
        h->AddStretchSpacer();
        h->Add(cancelbtn, 0, wxALL, 8);
        v->Add(h, 0, wxEXPAND);
        SetSizer(v);
        Centre();

        Bind(wxEVT_BUTTON, &ModUpdateDialog::OnCancel, this, cancelbtn->GetId());
        Bind(wxEVT_THREAD, &ModUpdateDialog::OnThreadUpdate, this);

        std::thread(&ModUpdateDialog::MainProc, this).detach();
    }

private:
    fs::path targetModsDir;        
    uint32 appid = 250900;

    wxListBox* statuslog;
    wxStaticText* progresstxt; 
    wxGauge* loadbar;
    wxButton* cancelbtn;

    std::thread mthread;
    std::atomic<bool> cancelrequest;

    void PostProgressEvent(int prc,const std::string& message) {
        if (!message.empty()) {
            if (message.starts_with("ERROR")) {
                Logger::Error(("[MODUPDATER] " + message + "\n").c_str());
            }
            else if (!message.starts_with("Processed ")) {
                Logger::Info(("[MODUPDATER] " + message + "\n").c_str());
            }
        }

        wxThreadEvent* evt = new wxThreadEvent(wxEVT_THREAD);
        evt->SetInt(prc);
        evt->SetString(message);
        if (!IsBeingDeleted()) {
            wxQueueEvent(this, evt);
        }
    }

    void OnCancel(wxCommandEvent&) {
        cancelrequest = true;
        cancelbtn->Disable();
        PostProgressEvent(0,"Cancel requested; finishing current file...");
    }

    void OnThreadUpdate(wxThreadEvent& evt) {
        int pct = evt.GetInt();
        wxString msg = evt.GetString();

        if (!msg.IsEmpty() && msg.StartsWith("Processed ")) { //sue me
            progresstxt->SetLabel(msg);
        }
        else {
            if (!msg.IsEmpty()) {
                if (msg.StartsWith("ERROR")) {
                    launcherlogger->LogError(("[MODUPDATER] " + msg).c_str());
                }
                else {
                    launcherlogger->LogInfo(("[MODUPDATER] " + msg).c_str());
                }
                statuslog->Insert(msg, 0);
                while (statuslog->GetCount() > 200) {
                    statuslog->Delete(statuslog->GetCount() - 1);
                }
            }
        }

        if (pct >= 0) {
            int val = pct;
            if (val < 0) val = 0;
            if (val > 100) val = 100;
            loadbar->SetValue(val);
        }

        if (!msg.IsEmpty() && msg.StartsWith("FINISH")) {
            EndModal(wxID_OK);
        }
    }

    void OnCloseNow(wxCommandEvent&) {
        EndModal(wxID_OK);
    }

    bool ParseMetadata(const fs::path& metadataPath, std::string& outName, std::string& outVersion) {
        try {
            rapidxml::file<> xmlFile(metadataPath.string().c_str());
            rapidxml::xml_document<> doc;
            doc.parse<0>(xmlFile.data());
            if (auto* root = doc.first_node("metadata")) {
                auto* nodeName = root->first_node("directory");
                auto* nodeVersion = root->first_node("version");
                outName = nodeName ? nodeName->value() : "";
                outVersion = nodeVersion ? nodeVersion->value() : "";
                return true;
            }
        }
        catch (const rapidxml::parse_error&) {
        }
        return false;
    }


    bool ParseMetadataId(const fs::path& xmlPath, uint64_t& outId) {
        try {
            rapidxml::file<> xmlFile(xmlPath.string().c_str());
            rapidxml::xml_document<> doc;
            doc.parse<0>(xmlFile.data());

            auto* root = doc.first_node("metadata");
            if (!root) return false;

            auto* idNode = root->first_node("id");
            if (!idNode || !idNode->value()) return false;

            outId = std::stoull(idNode->value());
            return true;
        }
        catch (...) {
            return false;
        }
    }

    void CopyDir(const fs::path& src, const fs::path& dst) {
        fs::create_directories(dst);
        for (auto& p : fs::recursive_directory_iterator(src)) {
            const auto rel = fs::relative(p.path(), src);
            const auto dstPath = dst / rel;
            if (fs::is_directory(p)) {
                fs::create_directories(dstPath);
            }
            else if (fs::is_regular_file(p) && (p.path().filename() != "metadata.xml")) {
                fs::create_directories(dstPath.parent_path());
                std::error_code ec;
                fs::copy_file(p.path(), dstPath,
                    fs::copy_options::overwrite_existing, ec);
            }
            if (cancelrequest) {
                PostProgressEvent(0, "Copying interrupted");
                return;
            }
        }
        std::error_code ec2;
        fs::copy_file(src / "metadata.xml", dst / "metadata.xml", fs::copy_options::overwrite_existing, ec2);
    }

    int CompareVersions(const std::string& a, const std::string& b) {
        try {
            auto split = [](const std::string& s) {
                std::vector<int> parts;
                std::stringstream ss(s);
                std::string token;
                while (std::getline(ss, token, '.')) {
                    parts.push_back(token.empty() ? 0 : std::stoi(token));
                }
                return parts;
            };

            std::vector<int> va = split(a);
            std::vector<int> vb = split(b);

            size_t n = (va.size() > vb.size()) ? va.size() : vb.size();
            va.resize(n, 0);
            vb.resize(n, 0);

            for (size_t i = 0; i < n; i++) {
                if (va[i] < vb[i]) return -1;
                if (va[i] > vb[i]) return 1;
            }
        }
        catch (...) {
                if (a != b) {
                    return -2;
                }
        }
        return 0;
    }

    void MainProc() {
        if (!SteamAPI_Init()) {
            PostProgressEvent(0, "Warning: SteamAPI_Init() failed or already initialized. Proceeding...");
            return;
        }
        if (!SteamAPI_IsSteamRunning()) {
            PostProgressEvent(0,"Steam is not running. Start Steam and try again.");
            PostProgressEvent(0,"DONE: Steam not running");
            return;
        }
        int overallPct = 0;

        uint32 num = SteamUGC()->GetNumSubscribedItems();
        if (num == 0) {
            PostProgressEvent(overallPct,"No subscribed workshop items found.");
            PostProgressEvent(overallPct,"DONE: nothing to update");
            return;
        }
        std::vector<PublishedFileId_t> subscribed(num);
        uint32 returned = SteamUGC()->GetSubscribedItems(subscribed.data(), num);
        if (returned == 0) {
            PostProgressEvent(overallPct,"Failed to retrieve subscribed items from Steam.");
            PostProgressEvent(overallPct,"DONE: failed to get subscriptions");
            return;
        }
        subscribed.resize(returned);
        PostProgressEvent(overallPct,"Found " + std::to_string(returned) + " subscribed items.");

        int totalToProcess = static_cast<int>(subscribed.size());
        int idx = 0;
        std::unordered_set<uint64> subscribedIds;

        PostProgressEvent(overallPct, "Checking mod versions for updating...");
        for (auto pfid : subscribed) {
            subscribedIds.insert(pfid);
            if (cancelrequest){ 
                PostProgressEvent(overallPct, "FINISH: Updating canceled!");
                return;
            }
            ++idx;
            uint64_t id = static_cast<uint64_t>(pfid);

            uint64_t sizeOnDisk = 0;
            uint32_t timeStamp = 0;
            char folderBuf[4096] = { 0 };
            bool ok = SteamUGC()->GetItemInstallInfo(pfid, &sizeOnDisk, folderBuf,
                (uint32)sizeof(folderBuf), &timeStamp);
            if (!ok) {
                PostProgressEvent(overallPct,"Mod " + std::to_string(id) + " is unavailable, privated by the author or Steam is still downloading it!"); //we can separately account for these 2 cases later, steamapi does provide the tools, but Im staying away from that for now for the initial versions
                overallPct = (idx * 100) / totalToProcess;
                PostProgressEvent(overallPct,"Processed " + std::to_string(idx) + " / " + std::to_string(totalToProcess));
                continue;
            }
            

            fs::path cachePath = fs::path(folderBuf);
            if (!fs::exists(cachePath) || !fs::is_directory(cachePath)) {
                PostProgressEvent(overallPct,"Install folder missing for " + std::to_string(id));
                overallPct = (idx * 100) / totalToProcess;
                PostProgressEvent(overallPct,"Processed " + std::to_string(idx) + " / " + std::to_string(totalToProcess));
                continue;
            }

            fs::path metadataPath = cachePath / "metadata.xml";
            if (!fs::exists(metadataPath)) {
                PostProgressEvent(overallPct,"Skipping " + std::to_string(id) + ": metadata.xml not found.");
                overallPct = (idx * 100) / totalToProcess;
                PostProgressEvent(overallPct,"Processed " + std::to_string(idx) + " / " + std::to_string(totalToProcess));
                continue;
            }

            std::string cacheName, cacheVersion;
            if (!ParseMetadata(metadataPath, cacheName, cacheVersion)) {
                PostProgressEvent(overallPct,"Failed to parse metadata for " + std::to_string(id));
                overallPct = (idx * 100) / totalToProcess;
                PostProgressEvent(overallPct,"Processed " + std::to_string(idx) + " / " + std::to_string(totalToProcess));
                continue;
            }
            if (cacheName.empty()) cacheName = "mod_" + std::to_string(id);
            fs::path installedFolder = targetModsDir / (cacheName + "_" + std::to_string(id));
            std::string installedVersion = "0";
            fs::path installedMetadata = installedFolder / "metadata.xml";
            if (fs::exists(installedMetadata)) {
                std::string inName, inVersion;
                if (ParseMetadata(installedMetadata, inName, inVersion))
                    installedVersion = inVersion.empty() ? "0" : inVersion;
            }

            int cmp = CompareVersions(installedVersion, cacheVersion);
            if ((cmp < 0) || (fs::exists(installedFolder / "Unfinished.it") || fs::exists(installedFolder / "Update.it"))) {
                if (cmp == -2) {
                    PostProgressEvent(overallPct, "ERROR Nonnumeric Mod Version for " + cacheName + " assuming outdated...");
                }
                PostProgressEvent(overallPct,"Updating " + cacheName + " (" + installedVersion + " -> " + cacheVersion + ")...");
                try {
                    CopyDir(cachePath, installedFolder);
                    fs::remove(installedFolder / "Unfinished.it"); //vanilla can still shove this shit in if interrumpted, we dont even use this here since we just copy the updated metadata.xml last....which makes unfinished.it pointless.
                    fs::remove(installedFolder / "Update.it"); //vanilla can still shove this shit in if interrumpted, we dont even use this here since we just copy the updated metadata.xml last....which makes unfinished.it pointless.
                    PostProgressEvent(overallPct,"DONE: Updated " + cacheName + " to version " + cacheVersion);
                }
                catch (...) {
                    PostProgressEvent(overallPct,"ERROR copying " + cacheName);
                }
            }

            //SteamAPI_RunCallbacks(); //Not needed anymore but leaving it here if I ever rework this shit to use it again (this is needed here if you are getting mod info from steam, because the steamapi needs this to be called in order to actually run the fucking steam callbacks, it's mental)

            overallPct = (idx * 100) / totalToProcess;
            PostProgressEvent(overallPct,"Processed " + std::to_string(idx) + " / " + std::to_string(totalToProcess));
        }


        PostProgressEvent(overallPct, "Checking unsubbed mods for deletion...");
        for (auto& entry : fs::directory_iterator(targetModsDir)) {
            if (!entry.is_directory())
                continue;

            const auto folderName = entry.path().filename().string();
            auto pos = folderName.rfind('_');
            if (pos == std::string::npos)
                continue;

            std::string idStr = folderName.substr(pos + 1);
            try {
                uint64 id = std::stoull(idStr);
                if (!subscribedIds.count(id)) {
                    fs::path metadataPath = entry.path() / "metadata.xml";
                    uint64_t metaId = 0;
                    if (!fs::exists(metadataPath) || !ParseMetadataId(metadataPath, metaId) || (metaId != id)) {
                        continue;
                    }
                    std::error_code ec;
                    fs::remove_all(entry.path(), ec);
                    if (ec)
                        PostProgressEvent(overallPct,"Failed to remove " + folderName + ": " + ec.message());
                    else
                        PostProgressEvent(overallPct,"DONE: Removed " + folderName);
                }
            }
            catch (...) {
            }
        }

        PostProgressEvent(overallPct,"FINISH: update process finished.");

    }
};
