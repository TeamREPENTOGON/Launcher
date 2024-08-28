#include <WinSock2.h>

#include <chrono>
#include <future>

#include "wx/cmdline.h"

#include "shared/externals.h"
#include "shared/github.h"
#include "shared/logger.h"
#include "self_updater/updater_window.h"

namespace Externals {
	void Init();
}

namespace Updater {
	struct CLIParams {
		wxString to, from;
		wxString url;
		wxString lockFilePath;
		bool debug;
	};

	static CLIParams params;

	char Updater::_printBuffer[Updater::BUFF_SIZE] = { 0 };

	bool App::OnInit() {
		Logger::Init("self_updater.log", "w");
		Externals::Init();

		ParseArgs();

		Updater* frame = new Updater();
		frame->Show();
		std::thread t(&Updater::DoUpdate, frame);
		t.detach();
		// frame->DoUpdate();
		return true;
	}

	void App::ParseArgs() const {
		wxCmdLineParser parser;
		LPSTR cli = GetCommandLineA();
		Logger::Info("App::ParseArgs cli = %s\n", cli);
		parser.SetSwitchChars("-");
		parser.SetCmdLine(cli);
		parser.AddOption("f", "from", "Version from which the upgrade is being performed");
		parser.AddOption("t", "to", "Version to which the upgrade is being performed");
		parser.AddOption("u", "url", "URL containing the release to download");
		parser.AddSwitch("d", "debug", "Enable debug features");
		parser.AddOption("l", "lock-file", "Path to the lock file used to synchronize with the launcher");
		// parser.Usage();
		parser.Parse(false);
		
		if (!parser.Found("f", &params.from)) {
			params.from = "unknown";
		}
		
		if (!parser.Found("t", &params.to)) {
			params.to = "unknown";
		}

		params.debug = parser.Found("d");

		if (!parser.Found("u", &params.url)) {
			wxMessageDialog dialog(NULL, "No URL specified, no update performed", "Fatal error");
			dialog.ShowModal();
			exit(-1);
		}

		if (!parser.Found("l", &params.lockFilePath)) {
			wxMessageDialog dialog(NULL, "No lock file specified, please run from the launcher", "Fatal error");
			dialog.ShowModal();
			exit(-1);
		}
	}

	Updater::Updater() : wxFrame(NULL, wxID_ANY, "REPENTOGON Launcher Updater") {
		SetSize(1024, 650);
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(sizer);
		_logWindow = new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, 400), wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH);
		sizer->Add(_logWindow, wxSizerFlags().Expand().Proportion(1).Border(wxLEFT | wxRIGHT | wxBOTTOM | wxTOP, 20));
		SetBackgroundColour(wxColour(237, 237, 237));
	}

	bool Updater::DoPreUpdateChecks() {
		UpdateStartupCheckResult startupCheck = _updater.DoStartupCheck();
		switch (startupCheck) {
		case UPDATE_STARTUP_CHECK_INVALID_FILE:
			LogError("Invalid lock file %s\n", _updater.GetLockFileName().c_str());
			return false;

		case UPDATE_STARTUP_CHECK_INVALID_CONTENT:
			LogError("Invalid lock file content\n");
			return false;

		case UPDATE_STARTUP_CHECK_INVALID_STATE:
			LogError("Lock file indicates invalid update state: expected %d, got %d\n", UPDATE_STATE_INIT, _updater.GetUpdateState());
			return false;

		case UPDATE_STARTUP_CHECK_CANNOT_FETCH_RELEASE:
			LogError("Unable to download release information from GitHub");
			LogGithubDownloadAsString("Download release information", _updater.GetReleaseDownloadResult());
			return false;

		case UPDATE_STARTUP_CHECK_INVALID_RELEASE_INFO:
			LogError("Release information is invalid");
			switch (_updater.GetReleaseInfoState()) {
			case RELEASE_INFO_STATE_NO_ASSETS:
				LogError("The release contains neither a hash file nor a launcher archive file");
				break;

			case RELEASE_INFO_STATE_NO_HASH:
				LogError("The release contains no hash file");
				break;

			case RELEASE_INFO_STATE_NO_ZIP:
				LogError("The release contains no launcher archive file");
				break;

			default:
				LogError("Sylmir forgot to handle this case");
				break;
			}
		}

		return true;
	}

	bool Updater::DownloadUpdate(LauncherUpdateData* updateData) {
		Threading::Monitor<Github::GithubDownloadNotification> monitor;
		std::future<bool> future = std::async(std::launch::async, &UpdaterBackend::DownloadUpdate,
			&_updater, updateData);
		size_t totalDownloadSize = 0;
		struct {
			Threading::Monitor<Github::GithubDownloadNotification>* monitor;
			std::string name;
			bool done;
		} monitorAndName[] = {
			{ &updateData->_hashMonitor, "Hash file", false },
			{ &updateData->_zipMonitor, "Launcher archive", false },
			{ NULL, "" }
		};

		std::chrono::steady_clock::time_point lastReceived = std::chrono::steady_clock::now();
		while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready && 
			std::any_of(monitorAndName, monitorAndName + 2, [](auto const& monitor) -> bool { return !monitor.done;  })) {
			for (auto s = monitorAndName; s->monitor; ++s) {
				while (std::optional<Github::GithubDownloadNotification> message = s->monitor->Get()) {
					switch (message->type) {
					case Github::GH_NOTIFICATION_INIT_CURL:
						Log("[%s] Initializing cURL connection to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_INIT_CURL_DONE:
						Log("[%s] Initialized cURL connection to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_CURL_PERFORM:
						Log("[%s] Performing cURL request to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_CURL_PERFORM_DONE:
						Log("[%s] Performed cURL request to %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_DATA_RECEIVED:
					{
						totalDownloadSize += std::get<size_t>(message->data);

						std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
						if (std::chrono::duration_cast<std::chrono::nanoseconds>(now - lastReceived).count() > 100000000) {
							Log("[%s] Downloaded %lu bytes\n", s->name.c_str(), totalDownloadSize);
							lastReceived = now;
						}
						break;
					}

					case Github::GH_NOTIFICATION_PARSE_RESPONSE:
						Log("[%s] Parsing result of cURL request from %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_PARSE_RESPONSE_DONE:
						Log("[%s] Parsed result of cURL request from %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					case Github::GH_NOTIFICATION_DONE:
						s->done = true;
						Log("[%s] Successfully downloaded content from %s\n", s->name.c_str(), std::get<std::string>(message->data).c_str());
						break;

					default:
						LogError("[%s] Unexpected asynchronous notification (id = %d)\n", s->name.c_str(), message->type);
						break;
					}
				}
			}
		}

		/* async synchronizes-with get, so all non atomic accesses become visible
		 * side-effects here. Therefore, there is no need to introduce a fence in
		 * order to read the content of updateData.
		 */
		return future.get();
	}

	bool Updater::PostDownloadChecks(bool downloadOk, LauncherUpdateData* data) {
		if (!downloadOk) {
			LogError("Error while downloading launcher update");

			if (data->_hashDownloadResult != Github::DOWNLOAD_AS_STRING_OK) {
				LogGithubDownloadAsString("hash download", data->_hashDownloadResult);
			}

			if (data->_zipDownloadResult != Github::DOWNLOAD_FILE_OK) {
				switch (data->_zipDownloadResult) {
				case Github::DOWNLOAD_FILE_BAD_CURL:
					LogError("Launcher archive: error while initializeing cURL connection to %s", data->_zipUrl.c_str());
					break;

				case Github::DOWNLOAD_FILE_BAD_FS:
					LogError("Launcher archive: error while creating file %s on disk", data->_zipFileName.c_str());
					break;

				case Github::DOWNLOAD_FILE_DOWNLOAD_ERROR:
					LogError("Launcher archive: error while downloading archive from %s", data->_zipUrl.c_str());
					break;

				default:
					LogError("Launcher archive: unexpected error code %d", data->_zipDownloadResult);
					break;
				}
			}

			return false;
		}
		else {
			Log("Successfully downloaded latest launcher version\n");
			Log("Checking hash consistency... ");

			// Trim the hash as it may contain terminating characters (\r\n)
			size_t i = 0;
			while (i < data->_hash.size() && (data->_hash[i] >= 'A' && data->_hash[i] <= 'F') ||
				(data->_hash[i] >= '0' && data->_hash[i] <= '9'))
				++i;

			if (i != data->_hash.size()) {
				data->_hash.resize(i);
			}

			if (!_updater.CheckHashConsistency(data->_zipFileName.c_str(), data->_hash.c_str())) {
				Log("KO\n");
				LogError("Hash mismatch: download was corrupted");
				return false;
			}
			else {
				Log("OK\n");
			}

			return true;
		}

		LogError("Sylmir probably forgot a return path in this function, please report this as an error");
		return false;
	}

	bool Updater::DoUpdate() {
		if (params.debug) {
			system("pause");
		}

		Log("Updating the REPENTOGON launcher\n");
		Log("Update scheduled from version %s to version %s\n", params.from.c_str().AsChar(), params.to.c_str().AsChar());
		Log("Using lock file %s\n", params.lockFilePath.c_str().AsChar());
		Log("Downloading release from %s\n", params.url.c_str().AsChar());

		new (&_updater) UpdaterBackend(params.lockFilePath.c_str().AsChar(), 
			params.from.c_str().AsChar(),
			params.to.c_str().AsChar(),
			params.url.c_str().AsChar());

		if (!DoPreUpdateChecks()) {
			return false;
		}

		LauncherUpdateData updateData;
		bool downloadResult = DownloadUpdate(&updateData);
		PostDownloadChecks(downloadResult, &updateData);
		return true;
	}

	void Updater::Log(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		Log("[INFO]", false, wxTextAttr(), fmt, va);
		va_end(va);
	}

	void Updater::LogError(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		wxTextAttr attr(*wxRED);
		Log("[ERROR]", false, attr, fmt, va);
		va_end(va);
	}

	void Updater::Log(const char* prefix, bool nl, wxTextAttr const& attr, const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		Log(prefix, nl, attr, fmt, va);
		va_end(va);
	}

	void Updater::Log(const char* prefix, bool nl, wxTextAttr const& attr, const char* fmt, va_list va) {
		wxString string;
		string.append(prefix);
		string.append(" ");

		int n = vsnprintf(NULL, 0, fmt, va);

		if (n < 0) {
			if (_logError) {
				MessageBoxA(NULL, "Unable to log text", "Internal fatal error", MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND | MB_OK);
				exit(-1);
			}

			_logError = true;
			Log("[ERROR]", false, "Low-level error when formatting log text\n");
			_logError = false;
			return;
		}

		char* buffer = _printBuffer;
		if (n >= 4096) {
			buffer = (char*)malloc(n + 1);
			if (!buffer) {
				_logError = true;
				Log("[ERROR]", false, "Unable to allocate memory to write log\n");
				_logError = false;
				return;
			}
		}

		vsnprintf(buffer, n + 1, fmt, va);

		string.append(buffer);
		if (nl && buffer[n] != '\n')
			string.append("\n");

		wxTextAttr original = _logWindow->GetDefaultStyle();
		_logWindow->SetDefaultStyle(attr);
		_logWindow->AppendText(string);
		_logWindow->SetDefaultStyle(original);

		if (n >= 4096)
			free(buffer);
	}

	void Updater::LogGithubDownloadAsString(const char* prefix, Github::DownloadAsStringResult result) {
		switch (result) {
		case Github::DOWNLOAD_AS_STRING_OK:
			Log("%s: successfully downloaded\n", prefix);
			break;

		case Github::DOWNLOAD_AS_STRING_BAD_CURL:
			LogError("%s: error while initiating cURL connection\n", prefix);
			break;

		case Github::DOWNLOAD_AS_STRING_BAD_REQUEST:
			LogError("%s: error while performing cURL request\n", prefix);
			break;

		case Github::DOWNLOAD_AS_STRING_INVALID_JSON:
			LogError("%s: malformed JSON in answer\n", prefix);
			break;

		case Github::DOWNLOAD_AS_STRING_NO_NAME:
			LogError("%s: JSON answer contains no \"name\" field\n", prefix);
			break;

		default:
			LogError("%s: Sylmir forgot a case somewhere\n", prefix);
			break;
		}
	}
}

wxIMPLEMENT_APP(Updater::App);