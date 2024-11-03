#include <algorithm>
#include <cstdio>
#include <future>
#include <filesystem>
#include <mutex>
#include <regex>
#include <set>
#include <thread>

#include "inih/ini.h"
#include "inih/cpp/INIReader.h"

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"

#include "wx/filectrl.h"
#include "wx/statline.h"

#include "comm/messages.h"
#include "curl/curl.h"
#include "launcher/advanced_options_window.h"
#include "launcher/installation.h"
#include "launcher/isaac.h"
#include "launcher/self_update.h"
#include "launcher/version.h"
#include "launcher/window.h"
#include "shared/github.h"
#include "shared/filesystem.h"
#include "shared/logger.h"
#include "shared/monitor.h"
#include "unpacker/unpacker_resources.h"
#include "launcher/self_updater/launcher_updater.h"

#include "zip.h"
#include "zipint.h"

#ifdef min
#undef min
#endif

wxBEGIN_EVENT_TABLE(Launcher::MainFrame, wxFrame)
EVT_COMBOBOX(Launcher::WINDOW_COMBOBOX_LEVEL, Launcher::MainFrame::OnLevelSelect)
EVT_COMBOBOX(Launcher::WINDOW_COMBOBOX_LAUNCH_MODE, Launcher::MainFrame::OnLauchModeSelect)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_CONSOLE, Launcher::MainFrame::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_UPDATES, Launcher::MainFrame::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_VANILLA_LUADEBUG, Launcher::MainFrame::OnOptionSelected)
EVT_TEXT(Launcher::WINDOW_TEXT_VANILLA_LUAHEAPSIZE, Launcher::MainFrame::OnCharacterWritten)
EVT_BUTTON(Launcher::WINDOW_BUTTON_LAUNCH_BUTTON, Launcher::MainFrame::Launch)
EVT_BUTTON(Launcher::WINDOW_BUTTON_SELECT_ISAAC, Launcher::MainFrame::OnIsaacSelectClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_SELECT_REPENTOGON_FOLDER, Launcher::MainFrame::OnSelectRepentogonFolderClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_ADVANCED_OPTIONS, Launcher::MainFrame::OnAdvancedOptionsClick)
wxEND_EVENT_TABLE()

namespace Launcher {
	static std::tuple<wxFont, wxFont> MakeBoldFont(wxWindow* window);
	static wxComboBox* CreateLevelsComboBox(wxWindow* window);

	// Read chunks of 1MB in the zip stream of REPENTOGON.zip
	static constexpr size_t StreamChunkSize = 1 << 20;

	/* Color of the error text if no isaac-ng.exe is specified. */
	static wxColor NoIsaacColor = *wxRED;
	/* Color of the error if no installation folder for Repentogon is specified. */
	static wxColor NoRepentogonInstallationFolderColor = wxColor(255, 128, 64);

	/* Error text displayed if no isaac-ng.exe is specified. */
	static const char* NoIsaacText = "No file specified, won't be able to launch anything";
	/* Error text displayed if no installation folder for Repentogon is specified. */
	static const char* NoRepentogonInstallationFolderText = "No folder specified, will download in current folder";

	static const char* GetCurrentDirectoryError = "unable to get current directory";
	
	MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "REPENTOGON Launcher"), _installation(this) {
		memset(&_options, 0, sizeof(_options));
		// _optionsGrid = new wxGridBagSizer(0, 20);
		_console = nullptr;
		_luaHeapSize = nullptr;

		SetSize(1024, 700);

		wxStaticBox* optionsBox = new wxStaticBox(this, -1, "Game configuration");
		wxStaticBoxSizer* optionsSizer = new wxStaticBoxSizer(optionsBox, wxHORIZONTAL);

		wxSizer* verticalSizer = new wxBoxSizer(wxVERTICAL);
		wxTextCtrl* logWindow = new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, 400), 
			wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH);
		logWindow->SetBackgroundColour(*wxWHITE);

		wxStaticBox* configurationBox = new wxStaticBox(this, -1, "Launcher configuration");
		wxStaticBoxSizer* configurationSizer = new wxStaticBoxSizer(configurationBox, wxHORIZONTAL);

		verticalSizer->Add(logWindow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 20);
		verticalSizer->Add(configurationSizer, 0, wxLEFT | wxEXPAND | wxRIGHT, 20);
		verticalSizer->Add(optionsSizer, 0, wxLEFT | wxEXPAND | wxRIGHT | wxTOP, 20);

		_logWindow = logWindow;
		_configurationBox = configurationBox;
		_configurationSizer = configurationSizer;
		_optionsSizer = optionsSizer;
		_optionsBox = optionsBox;

		AddLauncherConfigurationOptions();
		AddRepentogonOptions();
		AddVanillaOptions();
		AddLaunchOptions();
		
		wxButton* advancedOptionsButton = new wxButton(this, WINDOW_BUTTON_ADVANCED_OPTIONS, "Advanced options");
		verticalSizer->Add(advancedOptionsButton, 0, wxALIGN_RIGHT | wxRIGHT, 20);

		SetSizer(verticalSizer);

		SetBackgroundColour(wxColour(237, 237, 237));
	}

	MainFrame::~MainFrame() {
		_options.WriteConfiguration(this, _installation);
	}

	void MainFrame::AddLauncherConfigurationOptions() {
		wxBoxSizer* isaacSelectionSizer = new wxBoxSizer(wxHORIZONTAL);
		AddLauncherConfigurationTextField("Indicate the path of the isaac-ng.exe file",
			"Select file", NoIsaacText, 
			NoIsaacColor, isaacSelectionSizer, &_isaacFileText, WINDOW_BUTTON_SELECT_ISAAC);

		/* wxBoxSizer* repentogonSelectionSizer = new wxBoxSizer(wxHORIZONTAL);
		AddLauncherConfigurationTextField("Indicate folder in which to download and install REPENTOGON",
			"Select folder", 
			NoRepentogonInstallationFolderText, NoRepentogonInstallationFolderColor,
			repentogonSelectionSizer, &_repentogonInstallFolderText, WINDOW_BUTTON_SELECT_REPENTOGON_FOLDER); */

		// isaacSelectionSizer->Add(new wxStaticLine(), 0, wxBOTTOM | wxTOP, 20);

		_configurationSizer->Add(isaacSelectionSizer, 0, wxTOP | wxLEFT | wxRIGHT, 20);
		// _configurationSizer->Add(new wxStaticLine(), 0, wxTOP | wxBOTTOM, 5);
		// _configurationSizer->Add(repentogonSelectionSizer, 0, wxLEFT | wxRIGHT, 20);
		// _configurationSizer->Add(new wxStaticLine(), 0, wxBOTTOM, 20);
	}

	void MainFrame::AddLaunchOptions() {
		wxStaticBox* launchModeBox = new wxStaticBox(_optionsBox, -1, "Launch Options");
		wxStaticBoxSizer* launchModeBoxSizer = new wxStaticBoxSizer(launchModeBox, wxVERTICAL);

		wxSizer* box = new wxBoxSizer(wxHORIZONTAL);
		box->Add(new wxStaticText(launchModeBox, -1, "Launch mode: "));

		_launchMode = new wxComboBox(launchModeBox, WINDOW_COMBOBOX_LAUNCH_MODE);
		_repentogonLaunchModeIdx = _launchMode->Append("Repentogon");
		_launchMode->Append("Vanilla");
		_launchMode->SetValue("Repentogon");

		box->Add(_launchMode);

		launchModeBoxSizer->Add(box, 0, wxTOP | wxLEFT | wxRIGHT, 5);

		wxButton* launchButton = new wxButton(launchModeBox, WINDOW_BUTTON_LAUNCH_BUTTON, "Launch game");
		launchModeBoxSizer->Add(launchButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

		launchModeBoxSizer->Add(new wxStaticLine(launchModeBox), 0, wxBOTTOM, 5);

		_optionsSizer->Add(launchModeBoxSizer, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 10);
	}

	void MainFrame::AddRepentogonOptions() {
		wxStaticBox* repentogonBox = new wxStaticBox(_optionsBox, -1, "REPENTOGON Options");
		wxStaticBoxSizer* repentogonBoxSizer = new wxStaticBoxSizer(repentogonBox, wxVERTICAL);

		wxCheckBox* updates = new wxCheckBox(repentogonBox, WINDOW_CHECKBOX_REPENTOGON_UPDATES, "Automatically check for updates");
		updates->SetValue(true);

		wxCheckBox* console = new wxCheckBox(repentogonBox, WINDOW_CHECKBOX_REPENTOGON_CONSOLE, "Enable console window");
		console->SetValue(false);

		wxCheckBox* unstable = new wxCheckBox(repentogonBox, WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES, "Upgrade to unstable versions");
		unstable->SetValue(false);

		_updates = updates;
		_console = console;
		_unstableRepentogon = unstable;

		repentogonBoxSizer->Add(console, 0, wxTOP | wxLEFT | wxRIGHT, 5);
		repentogonBoxSizer->Add(updates, 0, wxLEFT | wxRIGHT, 5);
		repentogonBoxSizer->Add(unstable, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);


		_repentogonOptions = repentogonBox;
		_optionsSizer->Add(repentogonBoxSizer, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 10);
	}

	void MainFrame::AddVanillaOptions() {
		wxStaticBox* vanillaBox = new wxStaticBox(_optionsBox, -1, "Vanilla Options");
		wxStaticBoxSizer* vanillaBoxSizer = new wxStaticBoxSizer(vanillaBox, wxVERTICAL);

		wxSizer* levelSelectSizer = new wxBoxSizer(wxHORIZONTAL);
		levelSelectSizer->Add(new wxStaticText(vanillaBox, -1, "Starting stage: "));
		_levelSelect = CreateLevelsComboBox(vanillaBox);
		levelSelectSizer->Add(_levelSelect);

		vanillaBoxSizer->Add(levelSelectSizer, 0, wxTOP | wxLEFT | wxRIGHT, 5);
		_luaDebug = new wxCheckBox(vanillaBox, WINDOW_CHECKBOX_VANILLA_LUADEBUG, "Enable luadebug (unsafe)");
		vanillaBoxSizer->Add(_luaDebug, 0, wxLEFT | wxRIGHT, 5);
		vanillaBoxSizer->Add(new wxStaticLine(vanillaBox), 0, wxTOP | wxBOTTOM, 5);

		wxSizer* heapSizeBox = new wxBoxSizer(wxHORIZONTAL);
		wxTextValidator heapSizeValidator(wxFILTER_NUMERIC);
		wxTextCtrl* heapSizeCtrl = new wxTextCtrl(vanillaBox, WINDOW_TEXT_VANILLA_LUAHEAPSIZE, "1024");
		heapSizeCtrl->SetValidator(heapSizeValidator);
		_luaHeapSize = heapSizeCtrl;
		wxStaticText* heapSizeText = new wxStaticText(vanillaBox, -1, "Lua heap size (MB): ");
		heapSizeBox->Add(heapSizeText);
		heapSizeBox->Add(heapSizeCtrl);
		vanillaBoxSizer->Add(heapSizeBox, 0, wxLEFT | wxRIGHT, 20);
		vanillaBoxSizer->Add(new wxStaticLine(vanillaBox), 0, wxBOTTOM, 5);

		_optionsSizer->Add(vanillaBoxSizer, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 10);
	}

	void MainFrame::OnIsaacSelectClick(wxCommandEvent& event) {
		wxFileDialog dialog(this, "Please select the isaac-ng.exe to launch", wxEmptyString, wxEmptyString, 
			"isaac-ng.exe", wxFD_FILE_MUST_EXIST, wxDefaultPosition, wxDefaultSize, 
			"Select isaac-ng.exe executable");
		dialog.ShowModal();
		std::string path = dialog.GetPath().ToStdString();
		OnFileSelected(path, NoIsaacColor, _isaacFileText, NoIsaacText);
	}

	void MainFrame::OnSelectRepentogonFolderClick(wxCommandEvent& event) {
		wxDirDialog dialog(this, "Please select the folder in which to install Repentogon", wxEmptyString,
			0, wxDefaultPosition, wxDefaultSize, "Select Repentogon installation folder");
		dialog.ShowModal();

		std::string path = dialog.GetPath().ToStdString();
		OnFileSelected(path, NoRepentogonInstallationFolderColor, _repentogonInstallFolderText, NoRepentogonInstallationFolderText);
	}

	void MainFrame::OnFileSelected(std::string const& path, wxColor const& emptyColor, wxTextCtrl* ctrl, 
		const char* emptyText) {
		if (!path.empty()) {
			ctrl->SetValue(path);
		} else {
			wxTextAttr textAttr = ctrl->GetDefaultStyle();
			ctrl->Clear();
			ctrl->SetDefaultStyle(wxTextAttr(emptyColor));
			ctrl->AppendText(emptyText);
			ctrl->SetDefaultStyle(textAttr);
		}
	}

	void MainFrame::OnLevelSelect(wxCommandEvent& event) {
		wxComboBox* box = dynamic_cast<wxComboBox*>(event.GetEventObject());
		wxString string = box->GetValue();
		std::cmatch match;
		const char* text = string.c_str().AsChar();
		if (std::regex_search(text, match, std::basic_regex("([0-9])\\.([0-9])"))) {
			int stage = std::stoi(match[1].str(), NULL, 0);
			int type = std::stoi(match[2].str(), NULL, 0);
			_options.levelStage = stage;
			_options.stageType = type;
		} else if (!strcmp(text, "--")) {
			_options.levelStage = _options.stageType = 0;
		}
	}

	void MainFrame::OnLauchModeSelect(wxCommandEvent& event) {
		wxComboBox* box = dynamic_cast<wxComboBox*>(event.GetEventObject());
		if (box->GetValue() == "Vanilla") {
			_options.mode = LAUNCH_MODE_VANILLA;
		} else {
			_options.mode = LAUNCH_MODE_REPENTOGON;
		}

		UpdateRepentogonOptionsFromLaunchMode();
	}

	void MainFrame::OnCharacterWritten(wxCommandEvent& event) {
		wxTextCtrl* ctrl = dynamic_cast<wxTextCtrl*>(event.GetEventObject());
		_options.luaHeapSize = std::stoi(ctrl->GetValue().c_str().AsChar());
	}

	void MainFrame::OnOptionSelected(wxCommandEvent& event) {
		wxCheckBox* box = dynamic_cast<wxCheckBox*>(event.GetEventObject());
		switch (box->GetId()) {
		case WINDOW_CHECKBOX_REPENTOGON_CONSOLE:
			_options.console = box->GetValue();
			break;

		case WINDOW_CHECKBOX_REPENTOGON_UPDATES:
			_options.update = box->GetValue();
			break;

		case WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES:
			_options.unstableUpdates = box->GetValue();
			break;

		case WINDOW_CHECKBOX_VANILLA_LUADEBUG:
			_options.luaDebug = box->GetValue();
			break;

		default:
			return;
		}
	}

	void MainFrame::Launch(wxCommandEvent& event) {
		Log("Launching the game with the following options:");
		Log("\tRepentogon:");
		if (_options.mode == LAUNCH_MODE_VANILLA) {
			Log("\t\tRepentogon is disabled");
		} else {
			Log("\t\tRepentogon is enabled");
			Log("\t\tEnable Repentogon console window: %s", _options.console ? "yes" : "no");
		}
		Log("\tVanilla:");
		if (_options.levelStage) {
			Log("\t\tStarting stage: %d.%d", _options.levelStage, _options.stageType);
		} else {
			Log("\t\tStarting stage: not selected");
		}
		Log("\t\tLua debug: %s", _options.luaDebug ? "yes" : "no");
		Log("\t\tLua heap size: %dM", _options.luaHeapSize);

		_options.WriteConfiguration(this, _installation);
		::Launch(_isaacFileText->GetValue().c_str().AsChar(), _options);
	}

	bool MainFrame::InitializeIsaacFolderPath(bool shouldPrompt) {
		if (shouldPrompt) {
			std::string path = PromptIsaacInstallation();
			std::filesystem::path p(path);
			p.remove_filename();
			if (!_installation.ConfigureIsaacInstallationFolder(p.string())) {
				LogError("Unable to configure the Isaac installation folder");
				return false;
			} else {
				Log("Set Isaac installation folder to %s", p.string().c_str());
			}

			OnFileSelected(path, NoIsaacColor, _isaacFileText, NoIsaacText);
		} else {
			OnFileSelected(_installation.GetIsaacInstallationFolder() + "isaac-ng.exe",
				NoIsaacColor, _isaacFileText, NoIsaacText);
		}

		return true;
	}

	void MainFrame::HandleLauncherUpdates(bool allowPreReleases) {
		using lu = Updater::LauncherUpdateManager;
		std::string version, url;
		Updater::LauncherUpdateManager manager(this);
		lu::SelfUpdateCheckResult result = 
			manager.CheckSelfUpdateAvailability(allowPreReleases, version, url);
		if (result == lu::SELF_UPDATE_CHECK_UPDATE_AVAILABLE) {
			if (PromptLauncherUpdate(version, url)) {
				Log("Initiating update");
				if (!manager.DoUpdate(Launcher::version, version.c_str(), url.c_str())) {
					LogError("Error while updating the launcher\n");
				}
			} else {
				Log("Skipping self-update as per user choice");
			}
		} else if (result == lu::SELF_UPDATE_CHECK_ERR_GENERIC) {
			LogError("Error while checking for the availability of launcher updates");
		} else {
			Log("Launcher is up-to-date");
		}
	}

	bool MainFrame::SanityCheckLauncherUpdate() {
		return Filesystem::FileExists(Comm::UnpackedArchiveName);
	}

	void MainFrame::SanitizeLauncherUpdate() {
		LogWarn("Found launcher update file %s in the current folder."
			"This may indicate a broken update. Deleting it.\n", Comm::UnpackedArchiveName);
		Filesystem::RemoveFile(Comm::UnpackedArchiveName);
	}

	void MainFrame::PostInit() {
#ifdef LAUNCHER_UNSTABLE
		wxMessageBox("You are running an unstable version of the REPENTOGON launcher.\n"
			"If you wish to run a stable version, use the \"Self-update (stable version)\" button",
			"Unstable launcher version", wxCENTER | wxICON_WARNING);
		LogWarn("Running an unstable version of the launcher");
#endif
		Log("Welcome to the REPENTOGON Launcher (version %s)", Launcher::version);
		std::string currentDir = Filesystem::GetCurrentDirectory_();
		Log("Current directory is: %s", currentDir.c_str());

		if (!SanityCheckLauncherUpdate()) {
			SanitizeLauncherUpdate();
		}
		
		HandleLauncherUpdates(true);

		LPSTR cli = GetCommandLineA();
		Log("Command line: %s", cli);

		Log("Attempting to autodetect Isaac save folder, launcher configuration and Isaac installation folder...");
		bool shouldPromptForIsaac = !_installation.InitFolders();
		bool abortInitialization = false;
		while (!abortInitialization && !InitializeIsaacFolderPath(shouldPromptForIsaac)) {
			wxMessageDialog dialog(this, "Invalid Isaac folder path given, do you want to retry ? (Answering \"No\" will abort)", "Invalid folder", wxYES_NO);
			int result = dialog.ShowModal();
			abortInitialization = (result == wxID_NO);
		}

		if (abortInitialization) {
			Logger::Fatal("User did not provide a valid Isaac installation folder, aborting\n");
			wxAbort();
		}

		/* Guarantee: Isaac installation folder is okay, Repentogon installation
		 * has been identified.
		 */

		InitializeOptions();

		if (!_installation.IsCompatibleWithRepentogon()) {
			if (_repentogonLaunchModeIdx != -1) {
				_launchMode->Delete(_repentogonLaunchModeIdx);
				DisableRepentogonOptions();
			}
			return;
		}

		// Guarantee: installation is compatible with Repentogon.
		PostInitHandleRepentogon();
	}

	void MainFrame::PostInitHandleRepentogon() {
		bool needCheckRepentogonUpdates = true;
		bool successfullyInstalledRepentogon = false;
		bool attemptedToInstallRepentogon = false;
		bool attemptedToUpdateRepentogon = false;
		bool isLegacy = false;
		bool needToDisableRepentogonOptions = false;

		InstallationManager repentogonUpdateMgr(this, &_installation);

		if (_installation.GetRepentogonInstallationState() == REPENTOGON_INSTALLATION_STATE_NONE) {
			if (PromptRepentogonInstallation()) {
				attemptedToInstallRepentogon = true;
				repentogonUpdateMgr.InstallLatestRepentogon(true, _options.unstableUpdates);
				successfullyInstalledRepentogon = _installation.CheckRepentogonInstallation();
			}

			needCheckRepentogonUpdates = false;
		} else {
			isLegacy = _installation.IsLegacyRepentogonInstallation();
			// If an update occurs, this value is reassessed.
			needToDisableRepentogonOptions = isLegacy;
		}

		/* If installation is legacy, then an update is available _by construction_.
		 * Therefore there is no need to do a specific check.
		 */
		if (needCheckRepentogonUpdates && _options.update) {
			Log("Checking for Repentogon updates...");
			rapidjson::Document release;
			InstallationManager::CheckRepentogonUpdatesResult checkUpdates = 
				repentogonUpdateMgr.CheckRepentogonUpdates(release, _options.unstableUpdates, false);
			if (checkUpdates == InstallationManager::CHECK_REPENTOGON_UPDATES_UTD) {
				Log("Repentogon is up-to-date");
			} else if (checkUpdates == InstallationManager::CHECK_REPENTOGON_UPDATES_NEW) {
				Log("An update is available. Updating Repentogon...");
				attemptedToInstallRepentogon = true;
				attemptedToUpdateRepentogon = true;
				repentogonUpdateMgr.InstallRepentogon(release);
				successfullyInstalledRepentogon = _installation.CheckRepentogonInstallation();
			} else {
				LogError("Unable to check for availability of Repentogon updates\n");
				// No need to return, installation may be valid regardless
			}
		}
		
		if (attemptedToInstallRepentogon) {
			if (successfullyInstalledRepentogon) {
				if (_installation.IsLegacyRepentogonInstallation()) {
					Log("Legacy installation of Repentogon found. Repentogon will work, but the launcher cannot configure it.");
					needToDisableRepentogonOptions = true;
					isLegacy = true;
				} else {
					if (attemptedToUpdateRepentogon) {
						Log("Suscessfully updated Repentogon to version %s\n", _installation.GetRepentogonVersion().c_str());
					} else {
						Log("Successfully installed Repentogon version %s\n", _installation.GetRepentogonVersion().c_str());
					}

				}

				Log("State of the current Repentogon installation:\n");
				repentogonUpdateMgr.DisplayRepentogonFilesVersion(1, attemptedToUpdateRepentogon);
			} else {
				if (attemptedToUpdateRepentogon) {
					LogError("Unable to update Repentogon\n");
				} else {
					LogError("Unable to install Repentogon\n");
				}

				Log("Information regarding what happened during the installation:\n");
				repentogonUpdateMgr.DebugDumpBrokenRepentogonInstallation();
				needToDisableRepentogonOptions = true;
			}
		}

		if (needToDisableRepentogonOptions) {
			if (isLegacy) {
				LogWarn("Disabling Repentogon configuration options due to legacy installation\n");
			} else {
				LogWarn("Disabling Repentogon configuration options due to previous errors\n");
			}

			DisableRepentogonOptions();
		}
	}

	bool MainFrame::PromptRepentogonInstallation() {
		wxMessageDialog dialog(this, "No valid REPENTOGON installation found.\nDo you want to install now ?", "REPENTOGON Installation", wxYES_NO | wxCANCEL);
		int result = dialog.ShowModal();
		return result == wxID_OK || result == wxID_YES;
	}

	void MainFrame::Log(const char* prefix, bool nl, const char* fmt, ...) {
		char buffer[4096] = { 0 };
		size_t prefixLen = strlen(prefix);
		strncpy(buffer, prefix, 4096);

		{
			wxString text(buffer);
			std::unique_lock<std::mutex> lck(_logMutex);
			_logWindow->AppendText(text);

			memset(buffer, 0, sizeof(buffer));
		}

		va_list va;
		va_start(va, fmt);
		int count = vsnprintf(buffer, 4096, fmt, va);
		va_end(va);

		if (count <= 0)
			return;

		wxString text(buffer);
		if (buffer[count - 1] != '\n' && nl)
			text += '\n';

		std::unique_lock<std::mutex> lck(_logMutex);
		_logWindow->AppendText(text);
	}

	void MainFrame::Log(const char* fmt, ...) {
		char buffer[4096];
		va_list va;
		va_start(va, fmt);
		int count = vsnprintf(buffer, 4096, fmt, va);
		va_end(va);

		if (count <= 0)
			return;

		wxString text(buffer);
		if (buffer[count - 1] != '\n')
			text += '\n';

		std::unique_lock<std::mutex> lck(_logMutex);
		_logWindow->AppendText(text);
	}

	void MainFrame::LogNoNL(const char* fmt, ...) {
		char buffer[4096];
		va_list va;
		va_start(va, fmt);
		int count = vsnprintf(buffer, 4096, fmt, va);
		va_end(va);

		if (count <= 0)
			return;

		wxString text(buffer);
		std::unique_lock<std::mutex> lck(_logMutex);
		_logWindow->AppendText(text);
	}

	void MainFrame::LogInfo(const char* fmt, ...) {
		char buffer[4096];
		va_list va;
		va_start(va, fmt);
		int count = vsnprintf(buffer, 4096, fmt, va);
		va_end(va);

		if (count <= 0)
			return;

		wxString text(buffer);
		text.Prepend("[INFO] ");
		if (buffer[count - 1] != '\n')
			text += '\n';

		std::unique_lock<std::mutex> lck(_logMutex);
		_logWindow->AppendText(text);
	}

	void MainFrame::LogWarn(const char* fmt, ...) {
		char buffer[4096];
		va_list va;
		va_start(va, fmt);
		int count = vsnprintf(buffer, 4096, fmt, va);
		va_end(va);

		if (count <= 0)
			return;

		wxString text(buffer);
		text.Prepend("[WARN] ");
		if (buffer[count - 1] != '\n')
			text += '\n';

		wxTextAttr attr = _logWindow->GetDefaultStyle();
		wxColour color;
		color.Set(235, 119, 52);

		std::unique_lock<std::mutex> lck(_logMutex);
		_logWindow->SetDefaultStyle(wxTextAttr(color));
		_logWindow->AppendText(text);
		_logWindow->SetDefaultStyle(attr);
	}

	void MainFrame::LogError(const char* fmt, ...) {
		char buffer[4096];
		va_list va;
		va_start(va, fmt);
		int count = vsnprintf(buffer, 4096, fmt, va);
		va_end(va);

		if (count <= 0)
			return;

		wxString text(buffer);
		text.Prepend("[ERROR] ");
		if (buffer[count - 1] != '\n')
			text += '\n';

		wxTextAttr attr = _logWindow->GetDefaultStyle();
		std::unique_lock<std::mutex> lck(_logMutex);
		_logWindow->SetDefaultStyle(wxTextAttr(*wxRED));
		_logWindow->AppendText(text);
		_logWindow->SetDefaultStyle(attr);
	}

	std::tuple<wxFont, wxFont> MakeBoldFont(wxWindow* window) {
		wxFont source = window->GetFont();
		wxFont bold = source;
		bold.MakeBold();
		return std::make_tuple(source, bold);
	}

	wxComboBox* CreateLevelsComboBox(wxWindow* window) {
		wxComboBox* box = new wxComboBox(window, WINDOW_COMBOBOX_LEVEL, "Start level");

		int pos = 0;
		box->Insert(wxString("--"), pos++);
		box->SetValue("--");
		int variant = 0;
		const char** name = &(IsaacInterface::levelNames[0]);
		while (*name) {
			wxString s;
			int level = 1 + 2 * (variant / 5);
			box->Insert(s.Format("%s I (%d.%d)", *name, level, variant % 5), pos++, (void*)nullptr);
			box->Insert(s.Format("%s II (%d.%d)", *name, level + 1, variant % 5), pos++, (void*)nullptr);

			++variant;
			++name;
		}

		name = &(IsaacInterface::uniqueLevelNames[0]);
		while (*name) {
			box->Insert(wxString(*name), pos++, (void*)nullptr);
			++name;
		}

		return box;
	}

	void MainFrame::InitializeOptions() {
		std::unique_ptr<INIReader> const& reader = _installation.GetConfigurationFileParser();
		if (!reader) {
			Log("No launcher configuration file found, default initializing options\n");

			wxMessageDialog dialog(this, "Do you want to have the launcher automatically update Repentogon?\n(Selecting \"Yes\" will immediately update Repentogon to the latest versions)", "Automatic Repentogon updates", wxYES_NO | wxCANCEL);
			int result = dialog.ShowModal();

			wxMessageDialog unstableDialog(this, "Do you want to download unstable updates ?\n(If you are not a modder, you probably don't want this)", "Unstable Repentogon updates", wxYES_NO | wxCANCEL);
			int unstableResult = unstableDialog.ShowModal();

			_options.InitializeDefaults(this, result == wxID_OK || result == wxID_YES,
				result == wxID_OK || result == wxID_YES, 
				_installation.IsValidRepentogonInstallation(true));
		} else {
			Log("Found configuration file %s\n", _installation.GetLauncherConfigurationPath().c_str());
			_options.InitializeFromConfig(this, *reader,
				_installation.IsValidRepentogonInstallation(true));
		}
		
		InitializeGUIFromOptions();
	}

	void MainFrame::InitializeGUIFromOptions() {
		_console->SetValue(_options.console);
		_updates->SetValue(_options.update);
		_unstableRepentogon->SetValue(_options.unstableUpdates);

		// stringstream sucks
		char buffer[11];
		sprintf(buffer, "%d", _options.luaHeapSize);
		_luaHeapSize->SetValue(wxString(buffer));

		_luaDebug->SetValue(_options.luaDebug);
		InitializeLevelSelectFromOptions();
		if (_options.mode == LAUNCH_MODE_REPENTOGON) {
			_launchMode->SetValue("Repentogon");
		} else {
			_launchMode->SetValue("Vanilla");
		}

		UpdateRepentogonOptionsFromLaunchMode();
	}

	void MainFrame::InitializeLevelSelectFromOptions() {
		int level = _options.levelStage, type = _options.stageType;
		std::string value;
		if (level == 0) {
			value = "--";
		} else {
			std::string levelName;
			if (level <= IsaacInterface::STAGE4_2) {
				/* To find the level name, divide level by two to get the chapter - 1, then add the stage type. */
				levelName = IsaacInterface::levelNames[((level - 1) / 2) * 5 + type];
				if (level % 2 == 0) {
					levelName += " II";
				} else {
					levelName += " I";
				}

				char buffer[10];
				sprintf(buffer, " (%d.%d)", level, type);
				levelName += std::string(buffer);
			} else {
				levelName = IsaacInterface::uniqueLevelNames[level - IsaacInterface::STAGE4_3];
			}
			value = levelName;
		}

		_levelSelect->SetValue(wxString(value));
	}

	void MainFrame::UpdateRepentogonOptionsFromLaunchMode() {
		if (_launchMode->GetValue() == "Repentogon") {
			_repentogonOptions->Enable(true);
			// _console->Enable(true);
		} else {
			_repentogonOptions->Enable(false);
			// _console->Enable(false);
		}
	}

	void MainFrame::DisableRepentogonOptions() {
		_repentogonOptions->Disable();
		if (_repentogonLaunchModeIdx != -1) {
			_launchMode->Delete(_repentogonLaunchModeIdx);
		}
		_launchMode->SetValue("Vanilla");
	}

	void MainFrame::ForceRepentogonUpdate(bool allowPreReleases) {
		Log("Forcibly updating Repentogon to the latest version");

		InstallationManager repentogonUpdateMgr(this, &_installation);
		InstallationManager::DownloadInstallRepentogonResult result = repentogonUpdateMgr.InstallLatestRepentogon(true, allowPreReleases);

		if (result == InstallationManager::DOWNLOAD_INSTALL_REPENTOGON_ERR || 
			result == InstallationManager::DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES) {
			LogError("Unable to force Repentogon update\n");
			return;
		}

		if (!_installation.CheckRepentogonInstallation()) {
			LogError("Installation of Repentogon post update is not valid\n");
			repentogonUpdateMgr.DebugDumpBrokenRepentogonInstallation();
		} else {
			Log("State of the Repentogon installation:\n");
			repentogonUpdateMgr.DisplayRepentogonFilesVersion(1, true);
		}
	}

	std::string MainFrame::PromptIsaacInstallation() {
		wxString message("No Isaac installation found, please select isaac-ng.exe in the Isaac installation folder");
		// wxDirDialog dialog(this, message, wxEmptyString, wxDD_DIR_MUST_EXIST, wxDefaultPosition, wxDefaultSize, "Select Isaac folder");
		wxFileDialog dialog(this, message, wxEmptyString, wxEmptyString, "isaac-ng.exe", wxFD_FILE_MUST_EXIST, wxDefaultPosition,
			wxDefaultSize, "Select isaac-ng.exe executable");
		dialog.ShowModal();
		return dialog.GetPath().ToStdString();
	}

	void MainFrame::AddLauncherConfigurationTextField(const char* intro,
		const char* buttonText, const char* emptyText, wxColour const& emptyColor, 
		wxBoxSizer* sizer, wxTextCtrl** result, Launcher::Windows windowId) {
		_configurationSizer->Add(new wxStaticText(_configurationBox, -1, intro), 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 20);
		wxButton* isaacSelectionButton = new wxButton(_configurationBox, windowId, buttonText);
		_configurationSizer->Add(isaacSelectionButton, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);
		wxTextCtrl* textCtrl = new wxTextCtrl(_configurationBox, -1, wxEmptyString, wxDefaultPosition, wxSize(400, -1), wxTE_READONLY | wxTE_RICH);
		textCtrl->SetBackgroundColour(*wxWHITE);

		wxTextAttr textAttr = textCtrl->GetDefaultStyle();
		textCtrl->SetDefaultStyle(wxTextAttr(emptyColor));
		textCtrl->AppendText(emptyText);
		textCtrl->SetDefaultStyle(textAttr);

		_configurationSizer->Add(textCtrl, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);

		if (result) {
			*result = textCtrl;
		}
	}

	bool MainFrame::PromptLauncherUpdate(std::string const& version, std::string const& url) {
		std::ostringstream s;
		s << "An update is available for the launcher.\n" <<
			"It will update from version " << Launcher::version << " to version " << version << ".\n" <<
			"(You may also download manually from " << url << ").\n" <<
			"Do you want to update now ?";
		wxMessageDialog modal(this, s.str(), "Update Repentogon's Launcher ?", wxYES_NO);
		int result = modal.ShowModal();
		return result == wxID_YES || result == wxID_OK;
	}

	bool MainFrame::PromptLegacyUninstall() {
		std::ostringstream s;
		s << "An unsupported build of Repentogon is currently installed and must be removed in order to upgrade to the latest Repentgon version.\n"
			"Proceed with its uninstallation?";
		wxMessageDialog modal(this, s.str(), "Uninstall legacy Repentogon installation?", wxYES_NO);
		int result = modal.ShowModal();
		return result == wxID_YES || result == wxID_OK;
	}

	void MainFrame::OnAdvancedOptionsClick(wxCommandEvent& event) {
		AdvancedOptionsWindow window(this);
		int result = window.ShowModal();

		switch (result) {
		// User exited the modal without doing anything
		case wxID_CANCEL:
		case ADVANCED_EVENT_NONE:
			break;

		case ADVANCED_EVENT_FORCE_LAUNCHER_UNSTABLE_UPDATE:
			Updater::LauncherUpdateManager(this).ForceSelfUpdate(true);
			break;

		case ADVANCED_EVENT_FORCE_LAUNCHER_UPDATE:
			Updater::LauncherUpdateManager(this).ForceSelfUpdate(false);
			break;

		case ADVANCED_EVENT_FORCE_REPENTOGON_UNSTABLE_UPDATE:
			ForceRepentogonUpdate(true);
			break;

		case ADVANCED_EVENT_FORCE_REPENTOGON_UPDATE:
			ForceRepentogonUpdate(false);
			break;

		default:
			LogError("Unhandled result from ShowModal: %d", result);
			break;
		}
	}
}