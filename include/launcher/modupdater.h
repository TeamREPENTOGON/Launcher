#pragma once

#include <WinSock2.h>
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
#include <set>
#include <unordered_set>

#include "steam_api.h"
#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_utils.hpp"
#include "widgets/text_ctrl_log_widget.h"
#include "shared/filesystem.h"
#include "shared/logger.h"

namespace fs = std::filesystem;

// Relevant results from a QueryUGCDetailsRequest for a specific mod.
// While we can ask to skip fetching stuff like the description (thank god),
// fetching the NAME is not optional, so we may as well make use of it.
struct QueriedModDetails {
	std::string name;
	bool needsUpdate = false;
};

// Helper class to asynchronously send QueryUGCDetailsRequests to steam to identify mods that are outdated (and get names).
// A mod being "out of date" is strictly based on timestamps since Steam's local states are unreliable.
class QueryModDetailsHelper {
public:
	// Initializes an instance of the helper and sends the asynchronous batch queries to steam.
	static std::shared_ptr<QueryModDetailsHelper> CreateAndStart(const std::vector<PublishedFileId_t>& modsToCheck) {
		auto checker = std::shared_ptr<QueryModDetailsHelper>(new QueryModDetailsHelper(modsToCheck));
		checker->SendQueries();
		return checker;
	}

	// Returns true when all queries have completed.
	bool IsReady() const {
		return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}

	// Gets the final set of mods determined to be outdated.
	std::unordered_map<PublishedFileId_t, QueriedModDetails> GetResult() {
		return future_.get();
	}

	~QueryModDetailsHelper() {
		// Make sure that any pending requests are canceled, and the corresponding handles are freed.
		for (auto& callResult : sentCalls_) {
			if (callResult) {
				callResult->Cancel();
			}
		}

		// The requests have all been canceled, so the callbacks won't trigger now. Safe to clean up.
		sentCalls_.clear();

		for (UGCQueryHandle_t queryHandle : pendingHandles_) {
			if (queryHandle != k_UGCQueryHandleInvalid) {
				SteamUGC()->ReleaseQueryUGCRequest(queryHandle);
			}
		}
		pendingHandles_.clear();
	}

private:
	using CallResult = CCallResult<QueryModDetailsHelper, SteamUGCQueryCompleted_t>;
	static constexpr size_t MAX_BATCH_SIZE = 1000u;  // Max allowed by steam

	QueryModDetailsHelper() = delete;
	QueryModDetailsHelper(const QueryModDetailsHelper& other) = delete;
	QueryModDetailsHelper& operator=(const QueryModDetailsHelper& other) = delete;

	QueryModDetailsHelper(const std::vector<PublishedFileId_t>& modsToCheck) : modsToCheck_(modsToCheck) {
		future_ = promise_.get_future();
	}

	UGCQueryHandle_t CreateQuery(std::vector<PublishedFileId_t>& batch) {
		UGCQueryHandle_t queryHandle = SteamUGC()->CreateQueryUGCDetailsRequest(batch.data(), std::min(batch.size(), MAX_BATCH_SIZE));
		SteamUGC()->SetReturnLongDescription(queryHandle, false);
		SteamUGC()->SetReturnChildren(queryHandle, false);
		SteamUGC()->SetReturnKeyValueTags(queryHandle, false);
		SteamUGC()->SetReturnAdditionalPreviews(queryHandle, false);
		SteamUGC()->SetAllowCachedResponse(queryHandle, 0);
		return queryHandle;
	}

	// If the user has more than MAX_BATCH_SIZE subscribed items (lol), split the IDs up into batches.
	void SendQueries() {
		if (modsToCheck_.empty()) {
			// Nothing to check
			promise_.set_value(modDetails_);
			return;
		}

		std::set<UGCQueryHandle_t> queries;

		if (modsToCheck_.size() <= MAX_BATCH_SIZE) {
			// Only one query is required (most common)
			queries.insert(CreateQuery(modsToCheck_));
		} else {
			// What the fuck dude
			std::vector<PublishedFileId_t> batch;
			batch.reserve(MAX_BATCH_SIZE);
			for (const PublishedFileId_t id : modsToCheck_) {
				batch.push_back(id);
				if (batch.size() == MAX_BATCH_SIZE) {
					queries.insert(CreateQuery(batch));
					batch.clear();
				}
			}
			if (batch.size() > 0) {
				queries.insert(CreateQuery(batch));
			}
		}

		// Store the handles in case we need to free them later
		pendingHandles_ = queries;
		totalQueries_ = queries.size();

		// Send the queries
		for (const UGCQueryHandle_t queryHandle : queries) {
			SteamAPICall_t apiCallHandle = SteamUGC()->SendQueryUGCRequest(queryHandle);
			auto callResult = std::make_unique<CallResult>();
			callResult->Set(apiCallHandle, this, &QueryModDetailsHelper::HandleQueryCompleted);
			sentCalls_.push_back(std::move(callResult));
		}
	}

	// Callback function for whenever a query is completed.
	void HandleQueryCompleted(SteamUGCQueryCompleted_t* pResult, bool bIOFailure) {
		std::unique_lock<std::mutex> lock(lock_);

		if (!bIOFailure && pResult->m_eResult == k_EResultOK) {
			for (uint32 i = 0; i < pResult->m_unNumResultsReturned; ++i) {
				SteamUGCDetails_t details;
				if (SteamUGC()->GetQueryUGCResult(pResult->m_handle, i, &details)) {
					uint64_t sizeOnDisk = 0;
					uint32_t timestampOnDisk = 0;
					char folderBuf[4096] = { 0 };
					const bool installed = SteamUGC()->GetItemInstallInfo(
						details.m_nPublishedFileId,
						&sizeOnDisk,
						folderBuf,
						sizeof(folderBuf),
						&timestampOnDisk
					);

					QueriedModDetails& modDetails = modDetails_[details.m_nPublishedFileId];
					modDetails.name = details.m_rgchTitle;
					modDetails.needsUpdate = !installed || details.m_rtimeUpdated > timestampOnDisk;
				}
			}
		}

		SteamUGC()->ReleaseQueryUGCRequest(pResult->m_handle);
		pendingHandles_.erase(pResult->m_handle);

		numCompletedQueries_++;

		if (numCompletedQueries_ >= totalQueries_) {
			// All done
			promise_.set_value(modDetails_);
		}
	}

	std::vector<PublishedFileId_t> modsToCheck_;
	std::unordered_map<PublishedFileId_t, QueriedModDetails> modDetails_;

	std::mutex lock_;
	std::promise<std::unordered_map<PublishedFileId_t, QueriedModDetails>> promise_;
	std::shared_future<std::unordered_map<PublishedFileId_t, QueriedModDetails>> future_;

	std::vector<std::unique_ptr<CallResult>> sentCalls_;
	std::set<UGCQueryHandle_t> pendingHandles_;

	size_t totalQueries_ = 0;
	size_t numCompletedQueries_ = 0;
};

class ModUpdateDialog : public wxDialog {
public:
    wxTextCtrlLog* launcherlogger;
    uint64_t toupdate = 0;
    wxCheckBox* disablemoddownload;
    LauncherConfiguration* _configuration;

    ModUpdateDialog(wxWindow* parent,
        const fs::path& targetModsDir,wxTextCtrlLog* logger, uint64_t updateentryid = 0, LauncherConfiguration* config = nullptr)
        : wxDialog(parent, wxID_ANY, "Copying Mod files from Steam...",
            wxDefaultPosition, wxSize(600, 300)),
        targetModsDir(targetModsDir), cancelrequest(false)
    {
        toupdate = updateentryid;
        launcherlogger = logger;
        if (launcherlogger == nullptr) {
            wxTextCtrl* dummy = new wxTextCtrl();
            wxTextCtrlLog* dummylog = new wxTextCtrlLog(dummy);
            launcherlogger = dummylog;
        }

        wxBoxSizer* v = new wxBoxSizer(wxVERTICAL);
        statuslog = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 180));
        v->Add(statuslog, 1, wxEXPAND | wxALL, 8);

        progresstxt = new wxStaticText(this, wxID_ANY, "Processed 0 / ?");
        v->Add(progresstxt, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        loadbar = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 24));
        v->Add(loadbar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        wxBoxSizer* h = new wxBoxSizer(wxHORIZONTAL);

        if (config != nullptr) {
            _configuration = config;
            disablemoddownload = new wxCheckBox(this, wxID_ANY, "Skip waiting for mod downloading");
            disablemoddownload->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
                wxCheckBox* box = dynamic_cast<wxCheckBox*>(event.GetEventObject());
                _configuration->SetSkipWaitModDownloads(box->GetValue());
                canceldownloads = box->GetValue();
            });
            disablemoddownload->SetValue(_configuration->SkipWaitModDownloads());
            canceldownloads = _configuration->SkipWaitModDownloads();
            h->Add(disablemoddownload,0,wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,5);        
            h->AddStretchSpacer();
        }
        cancelbtn = new wxButton(this, wxID_CANCEL, "Cancel");
        h->AddStretchSpacer();
        h->Add(cancelbtn, 0, wxALL, 8);
        v->Add(h, 0, wxEXPAND);
        SetSizer(v);
        Centre();

		timer_ = std::make_unique<wxTimer>(this, wxID_ANY);
		timer_->Start(100);

        Bind(wxEVT_BUTTON, &ModUpdateDialog::OnCancel, this, cancelbtn->GetId());
        Bind(wxEVT_THREAD, &ModUpdateDialog::OnThreadUpdate, this);
		Bind(wxEVT_TIMER, &ModUpdateDialog::OnTimer, this);

        std::thread(&ModUpdateDialog::MainProc, this).detach();
    }

private:
    fs::path targetModsDir;
    uint32 appid = 250900;

    wxListBox* statuslog;
    wxStaticText* progresstxt;
    wxGauge* loadbar;
    wxButton* cancelbtn;
	std::unique_ptr<wxTimer> timer_ = nullptr;

    std::thread mthread;
    std::atomic<bool> cancelrequest;
    std::atomic<bool> canceldownloads;

    void PostProgressEvent(int prc,const std::string& message) {
        if (!message.empty()) {
            if (message.starts_with("ERROR")) {
                Logger::Error(("[MODUPDATER] " + message + "\n").c_str());
            }
            else if (!message.starts_with("Processed ") && !message.starts_with("Preparing ") && !message.starts_with("Downloading ") && !message.starts_with("Attempting ")) { // feel like suing somebody
                Logger::Info(("[MODUPDATER] " + message + "\n").c_str());
            }
        }

        wxThreadEvent* evt = new wxThreadEvent(wxEVT_THREAD);
        evt->SetInt(prc);
        evt->SetString(wxString::FromUTF8(message));
        if (!IsBeingDeleted()) {
            wxQueueEvent(this, evt);
        }
    }

    void OnCancel(wxCommandEvent&) {
        cancelrequest = true;
        cancelbtn->Disable();
        PostProgressEvent(0,"Cancel requested; finishing current file...");
    }

	void OnTimer(wxTimerEvent&) {
		if (loadbar->GetValue() == 0 || loadbar->GetValue() == 100) {
			loadbar->Pulse();
		}
	}

    void OnThreadUpdate(wxThreadEvent& evt) {
        int pct = evt.GetInt();
        wxString msg = evt.GetString();

        if (!msg.IsEmpty() && (msg.StartsWith("Processed ") || msg.StartsWith("Downloading ") || msg.StartsWith("Done with ") || msg.StartsWith("Preparing ") || msg.StartsWith("Attempting "))) { //sue me thrice
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
			if (val != loadbar->GetValue()) {
				loadbar->SetValue(val);
			}
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
            std::ifstream file(metadataPath, std::ios::binary);
			if (!file) {
				Logger::Error("[MODUPDATER::ParseMetadata] Failed to open `%s`: %s\n", metadataPath.string().c_str(), std::strerror(errno));
				return false;
			}
            rapidxml::file<> xmlFile(file);
            rapidxml::xml_document<> doc;
            doc.parse<0>(xmlFile.data());
            if (auto* root = doc.first_node("metadata")) {
                auto* nodeName = root->first_node("directory");
                auto* nodeVersion = root->first_node("version");
                outName = nodeName ? nodeName->value() : "";
				// Shoutout to minecraft explosions mod and Sacrilege (on its initial release)
				outVersion = nodeVersion ? nodeVersion->value() : "0";
				if (outVersion.empty()) {
					outVersion = "0";
				}
                return true;
			} else {
				Logger::Error("[MODUPDATER::ParseMetadata] Failed to parse metadata from `%s`\n", metadataPath.string().c_str());
			}
        }
        catch (const rapidxml::parse_error& err) {
			Logger::Error("[MODUPDATER::ParseMetadata] XML parse error parsing `%s`: %s\n", metadataPath.string().c_str(), err.what());
        }
        return false;
    }


    bool ParseMetadataId(const fs::path& xmlPath, uint64_t& outId) {
        try {
            std::ifstream file(xmlPath, std::ios::binary);
			if (!file) {
				Logger::Error("[MODUPDATER::ParseMetadataId] Failed to open `%s`: %s\n", xmlPath.string().c_str(), std::strerror(errno));
				return false;
			}
            rapidxml::file<> xmlFile(file);
            rapidxml::xml_document<> doc;
            doc.parse<0>(xmlFile.data());

            auto* root = doc.first_node("metadata");
			if (!root) {
				Logger::Error("[MODUPDATER::ParseMetadataId] Failed to parse metadata from `%s`\n", xmlPath.string().c_str());
				return false;
			}

            auto* idNode = root->first_node("id");
            if (!idNode || !idNode->value()) return false;

            outId = std::stoull(idNode->value());
            return true;
        }
		catch (const std::exception& err) {
			Logger::Error("[MODUPDATER::ParseMetadataId] Caught exception while parsing `%s`: %s\n", xmlPath.string().c_str(), err.what());
        }
		return false;
    }

    void CopyDir(const fs::path& src, const fs::path& dst) {
        fs::create_directories(dst);
        for (auto& p : fs::directory_iterator(dst)) { //clear out mod contents before copying over the steam shit to avoid keeping deleted files
            const auto& path = p.path();
            if ((path.filename() == "metadata.xml") || (path.extension() == ".it"))
                continue;
            fs::remove_all(path);
        }
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
                fs::remove_all(dst);
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

    bool SteamDownloadNWait(int* overallPct, uint64_t id, const std::string& downloadingmodname) {
        if (canceldownloads || (toupdate > 0)) { return false; }
        if (!SteamUGC()->DownloadItem(id, true)) {
            PostProgressEvent(*overallPct, "Download Failed! (steam couldnt get the mod)");
            return false;
        }

        uint64 bytesDownloaded = 0;
        uint64 bytesTotal = 0;

        bool started = false;
        int fallbackprc = 0;

        PostProgressEvent(*overallPct, "Attempting to download " + downloadingmodname + " cache (Waiting for Steam)");
        while (!cancelrequest && !canceldownloads) {
            SteamAPI_RunCallbacks();

            uint32 state = SteamUGC()->GetItemState(id); 

            if (state & k_EItemStateDownloading) {
                started = true;
                fallbackprc = 100;
                if (SteamUGC()->GetItemDownloadInfo(id, &bytesDownloaded, &bytesTotal)) {
                    if ((bytesTotal > 0)) {
                        int pct = static_cast<int>((bytesDownloaded * 100) / bytesTotal);
                        double div = 1024 * 1024;
                        std::string unity = "mb";
                        if ((bytesTotal / div) < 1) {
                            div = 1024;
                            unity = "kb";
                        }
                        double progress = bytesDownloaded / div;
                        double total = bytesTotal / div;
                        std::ostringstream progressStr;
                        progressStr << std::fixed << std::setprecision(2) << progress;

                        std::ostringstream totalStr;
                        totalStr << std::fixed << std::setprecision(2) << total;

						if (bytesDownloaded == bytesTotal) {
							PostProgressEvent(pct, "Preparing " + downloadingmodname + " cache (Waiting for Steam)");
						} else {
							PostProgressEvent(pct, "Downloading " + downloadingmodname + " (" + progressStr.str() + unity + " / " + totalStr.str() + unity + ")");
						}
                    }
                    else {
                        PostProgressEvent(fallbackprc, "Preparing " + downloadingmodname + " cache (Waiting for Steam)");
                    }
                }
                else {
                    PostProgressEvent(*overallPct, "Done with " + downloadingmodname + " cache...");
                    return true;
                }
            }
            else if ((state & k_EItemStateDownloadPending) == 0) {
                PostProgressEvent(*overallPct, "Done with " + downloadingmodname + " cache...");
                return true;
            }
            else {
                PostProgressEvent(fallbackprc, "Preparing " + downloadingmodname + " cache (Waiting for Steam)");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        return false;
    }

    void MainProc() {
        if (!SteamAPI_Init()) {
            PostProgressEvent(0, "ERROR: Failed to initialize Steam API");
            PostProgressEvent(0, "FINISH: update process finished.");
            return;
        }
        if (!SteamAPI_IsSteamRunning()) {
            PostProgressEvent(0,"ERROR: Steam is not running. Start Steam and try again.");
            PostProgressEvent(0,"DONE: Steam not running");
            PostProgressEvent(0, "FINISH: update process finished.");
            return;
        }
		if (!SteamUGC()) {
			PostProgressEvent(0, "ERROR: Failed to connect to the Steam Workshop");
			PostProgressEvent(0, "FINISH: update process finished.");
			return;
		}

        int overallPct = 0;

        uint32 num = SteamUGC()->GetNumSubscribedItems();
        /*if (num == 0) {
            PostProgressEvent(overallPct,"No subscribed workshop items found.");
            PostProgressEvent(overallPct,"DONE: nothing to update");
            PostProgressEvent(overallPct, "FINISH: update process finished.");
            return;
        }*/
        std::vector<PublishedFileId_t> subscribed(num);
        uint32 returned = SteamUGC()->GetSubscribedItems(subscribed.data(), num);
        if (num > 0 && returned == 0) {
            PostProgressEvent(overallPct,"Failed to retrieve subscribed items from Steam.");
            PostProgressEvent(overallPct,"DONE: failed to get subscriptions");
            PostProgressEvent(overallPct, "FINISH: update process finished.");
            return;
        }
        subscribed.resize(returned);
        PostProgressEvent(overallPct,"Found " + std::to_string(returned) + " subscribed items.");

        int totalToProcess = static_cast<int>(subscribed.size());
        int idx = 0;
        std::unordered_set<uint64> subscribedIds;

        if (toupdate > 0) {
            subscribed.clear();
            subscribed.push_back(toupdate);
            totalToProcess = 1;
            PostProgressEvent(overallPct, "Reinstalling requested mod...");
        }
        else {
            PostProgressEvent(overallPct, "Checking mod versions for updating...");
        }
		PostProgressEvent(overallPct, "Processed 0 / " + std::to_string(totalToProcess));

		// If downloads are allowed, check for updates by sending a batch query to Steam to fetch/compare timestamps and names.
		std::unordered_map<PublishedFileId_t, QueriedModDetails> queriedModDetails;
		if (!subscribed.empty() && toupdate == 0 && !canceldownloads && !cancelrequest) {
			auto checker = QueryModDetailsHelper::CreateAndStart(subscribed);
			std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
			while (!checker->IsReady() && !canceldownloads && !cancelrequest) {
				const int64_t secondsElapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count();
				if (secondsElapsed >= 10) {
					// Enforce our own hard timeout just in case the request REALLY hangs somehow.
					break;
				}
				SteamAPI_RunCallbacks();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (checker->IsReady()) {
				queriedModDetails = checker->GetResult();
			}
		}

        for (auto pfid : subscribed) {
            subscribedIds.insert(pfid);
            if (cancelrequest){
                PostProgressEvent(overallPct, "FINISH: Updating canceled!");
                return;
            }
            ++idx;
            uint64_t id = static_cast<uint64_t>(pfid);

			// Name string that we can use prior to parsing metadata.
			// If we successfully retrieved mod details from Steam, we can use that name.
			// Otherwise, just use the ID.
			std::string displayName = queriedModDetails[id].name;
			if (displayName.empty()) {
				displayName = std::to_string(id);
			}

            uint64_t sizeOnDisk = 0;
            uint32_t timeStamp = 0;
            char folderBuf[4096] = { 0 };
            bool ok = SteamUGC()->GetItemInstallInfo(pfid, &sizeOnDisk, folderBuf,
                (uint32)sizeof(folderBuf), &timeStamp);
            if (!ok || queriedModDetails[id].needsUpdate) {
				if (!ok) {
					PostProgressEvent(overallPct, "DOWNLOADING MOD: " + displayName);
				} else {
					PostProgressEvent(overallPct, "DOWNLOADING UPDATE: " + displayName);
				}
                
                //ModManagerReinstallDialog(this, id, std::to_wstring(id)).ShowModal(); // cant do this on a thread anyway...
                if (!SteamDownloadNWait(&overallPct, id, displayName) || !SteamUGC()->GetItemInstallInfo(pfid, &sizeOnDisk, folderBuf,
                    (uint32)sizeof(folderBuf), &timeStamp)) {
                    PostProgressEvent(overallPct, "Mod " + displayName + " failed to download or was canceled!");
                    overallPct = (idx * 100) / totalToProcess;
                    PostProgressEvent(overallPct,"Processed " + std::to_string(idx) + " / " + std::to_string(totalToProcess));
                    continue;
                }
            }


            fs::path cachePath = fs::path(folderBuf);
            if (!Filesystem::SafeExists(cachePath) || !fs::is_directory(cachePath)) { //this happens when cache gets fucked and steam still believes it got cache for this mod AND WONT DOWNLOAD IT NATURALLY
                PostProgressEvent(overallPct, "Cache folder missing for " + displayName);

                if (!SteamDownloadNWait(&overallPct, id, displayName) || !Filesystem::SafeExists(cachePath) || !fs::is_directory(cachePath)) {
                    overallPct = (idx * 100) / totalToProcess;
                    PostProgressEvent(overallPct, "Processed " + std::to_string(idx) + " / " + std::to_string(totalToProcess));
                    continue;
                }
            }

            fs::path metadataPath = cachePath / "metadata.xml";
            if (!Filesystem::SafeExists(metadataPath)) { //this happens if cache is corrupted
				PostProgressEvent(overallPct, "metadata.xml missing for " + displayName);

                if (!SteamDownloadNWait(&overallPct, id, displayName) || !Filesystem::SafeExists(metadataPath)) {
                    PostProgressEvent(overallPct, "Skipping " + displayName + ": metadata.xml not found.");
                    overallPct = (idx * 100) / totalToProcess;
                    PostProgressEvent(overallPct, "Processed " + std::to_string(idx) + " / " + std::to_string(totalToProcess));
                    continue;
                }
            }

            std::string cacheName, cacheVersion;
            if (!ParseMetadata(metadataPath, cacheName, cacheVersion)) {
                PostProgressEvent(overallPct,"Failed to parse metadata for " + displayName);
                overallPct = (idx * 100) / totalToProcess;
                PostProgressEvent(overallPct,"Processed " + std::to_string(idx) + " / " + std::to_string(totalToProcess));
                continue;
            }
			if (cacheName.empty()) {
				cacheName = "mod_" + std::to_string(id);
			} else if (displayName == std::to_string(id)) {
				displayName = cacheName;
			}
            fs::path installedFolder = targetModsDir / (cacheName + "_" + std::to_string(id));
            std::string installedVersion = "0";
            fs::path installedMetadata = installedFolder / "metadata.xml";

			bool installationExists = Filesystem::SafeExists(installedMetadata);
			bool shouldUpdate = !installationExists;

			if (installationExists) {
                std::string inName, inVersion;
                if (ParseMetadata(installedMetadata, inName, inVersion))
                    installedVersion = inVersion;
				int cmp = CompareVersions(installedVersion, cacheVersion);
				if (cmp < 0) {
					if (cmp == -2) {
						PostProgressEvent(overallPct, "ERROR Nonnumeric Mod Version for " + cacheName + " assuming outdated...");
					}
					shouldUpdate = true;
				}

				if (!shouldUpdate) {
					if (Filesystem::SafeExists(installedFolder / "Unfinished.it")) {
						Logger::Info("[MODUPDATER] Updating `%s` due to presence of `Unfinished.it`\n", cacheName.c_str());
						shouldUpdate = true;
					} else if (Filesystem::SafeExists(installedFolder / "Update.it")) {
						Logger::Info("[MODUPDATER] Updating `%s` due to presence of `Update.it`\n", cacheName.c_str());
						shouldUpdate = true;
					}
				}
            }

            if (shouldUpdate) {
				if (!installationExists) {
					PostProgressEvent(overallPct, "Installing " + cacheName + " (version " + cacheVersion + ")...");
				} else {
					PostProgressEvent(overallPct, "Updating " + cacheName + " (" + installedVersion + " -> " + cacheVersion + ")...");
				}

                try {
                    std::ofstream(installedFolder / "Unfinished.it"); //there was some oddc ase of going back and forth between vanilla and rgon with unfinished mods so I still need to use this :(
                    CopyDir(cachePath, installedFolder);
                    if (cancelrequest) {
                        PostProgressEvent(overallPct, "FINISH: Updating canceled!");
                        Logger::Warn("[MODUPDATER] Canceled mid-copy folder discarded! \n");
                        return;
                    }
                    fs::remove(installedFolder / "Unfinished.it");
                    fs::remove(installedFolder / "Update.it"); //vanilla can still shove this shit in if interrumpted, we dont even use this here since we just copy the updated metadata.xml last....which makes unfinished.it pointless.
                    PostProgressEvent(overallPct,"DONE: Updated " + displayName + " to version " + cacheVersion);
                }
				catch (const std::exception& err) {
					Logger::Error("[MODUPDATER] Caught exception while updating `%s`: %s\n", cacheName.c_str(), err.what());
					PostProgressEvent(overallPct, "ERROR copying " + cacheName);
				}
            }

            //SteamAPI_RunCallbacks(); //Not needed anymore but leaving it here if I ever rework this shit to use it again (this is needed here if you are getting mod info from steam, because the steamapi needs this to be called in order to actually run the fucking steam callbacks, it's mental)

            overallPct = (idx * 100) / totalToProcess;
            PostProgressEvent(overallPct,"Processed " + std::to_string(idx) + " / " + std::to_string(totalToProcess));
        }
        if (toupdate > 0) {
            PostProgressEvent(overallPct, "FINISH: mod reinstall process finished.");
            return;
        }

        PostProgressEvent(overallPct, "Checking unsubbed mods for deletion...");
        for (auto& entry : fs::directory_iterator(targetModsDir)) {
            if (!entry.is_directory())
                continue;

            const auto folderName = entry.path().filename().wstring();
            auto pos = folderName.rfind('_');
            if (pos == std::string::npos)
                continue;

            std::wstring idStr = folderName.substr(pos + 1);
            try {
                uint64 id = std::stoull(idStr);
                if (!subscribedIds.count(id)) {
                    fs::path metadataPath = entry.path() / "metadata.xml";
                    uint64_t metaId = 0;
                    if (!Filesystem::SafeExists(metadataPath) || !ParseMetadataId(metadataPath, metaId) || (metaId != id)) {
                        continue;
                    }
                    std::error_code ec;
                    fs::remove_all(entry.path(), ec);
                    if (ec)
                        PostProgressEvent(overallPct,"Failed to remove " + wxString(folderName).ToStdString() + ": " + ec.message());
                    else
                        PostProgressEvent(overallPct,"DONE: Removed " + wxString(folderName).ToStdString());
                }
            }
            catch (...) {
            }
        }

        PostProgressEvent(overallPct,"FINISH: update process finished.");

    }
};
