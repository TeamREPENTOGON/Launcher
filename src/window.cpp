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

#include "curl/curl.h"
#include "launcher/filesystem.h"
#include "launcher/isaac.h"
#include "launcher/self_update.h"
#include "launcher/window.h"
#include "shared/github.h"
#include "shared/filesystem.h"
#include "shared/logger.h"
#include "shared/monitor.h"
#include "self_updater/updater_resources.h"
#include "self_updater/updater.h"

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
EVT_BUTTON(Launcher::WINDOW_BUTTON_FORCE_UPDATE, Launcher::MainFrame::ForceUpdate)
EVT_BUTTON(Launcher::WINDOW_BUTTON_FORCE_UNSTABLE_UPDATE, Launcher::MainFrame::ForceUpdate)
EVT_BUTTON(Launcher::WINDOW_BUTTON_SELECT_ISAAC, Launcher::MainFrame::OnIsaacSelectClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_SELECT_REPENTOGON_FOLDER, Launcher::MainFrame::OnSelectRepentogonFolderClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_SELF_UPDATE, Launcher::MainFrame::OnSelfUpdateClick)
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

	
	MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "REPENTOGON Launcher"), 
		_installationManager(this) {
		memset(&_options, 0, sizeof(_options));
		// _optionsGrid = new wxGridBagSizer(0, 20);
		_console = nullptr;
		_luaHeapSize = nullptr;

		SetSize(1024, 650);

		wxBoxSizer* optionsSizer = new wxBoxSizer(wxHORIZONTAL);
		wxStaticBox* optionsBox = new wxStaticBox(this, -1, "Game configuration");
		optionsBox->SetSizer(optionsSizer);

		wxSizer* verticalSizer = new wxBoxSizer(wxVERTICAL);
		wxTextCtrl* logWindow = new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, 400), 
			wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH);
		logWindow->SetBackgroundColour(*wxWHITE);

		wxStaticBox* configurationBox = new wxStaticBox(this, -1, "Launcher configuration");
		wxBoxSizer* configurationSizer = new wxBoxSizer(wxVERTICAL);
		configurationBox->SetSizer(configurationSizer);

		wxStaticBox* advanced = new wxStaticBox(this, -1, "Advanced options");
		wxBoxSizer* advancedSizer = new wxBoxSizer(wxVERTICAL);
		advanced->SetSizer(advancedSizer);

		verticalSizer->Add(logWindow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 20);
		verticalSizer->Add(configurationBox, 0, wxLEFT | wxEXPAND | wxRIGHT, 20);
		verticalSizer->Add(optionsBox, 0, wxLEFT | wxEXPAND | wxRIGHT | wxTOP, 20);
		verticalSizer->Add(advanced, 0, wxLEFT | wxEXPAND | wxRIGHT | wxTOP, 20);

		_logWindow = logWindow;
		_configurationBox = configurationBox;
		_configurationSizer = configurationSizer;
		_optionsSizer = optionsSizer;
		_optionsBox = optionsBox;
		_advancedOptions = advanced;
		_advancedSizer = advancedSizer;

		AddLauncherConfigurationOptions();
		AddRepentogonOptions();
		AddVanillaOptions();
		AddLaunchOptions();
		AddAdvancedOptions();

		SetSizer(verticalSizer);

		SetBackgroundColour(wxColour(237, 237, 237));
	}

	MainFrame::~MainFrame() {
		_options.WriteConfiguration(this, _installationManager._installation);
	}

	void MainFrame::AddAdvancedOptions() {
		wxButton* button = new wxButton(_advancedOptions, Launcher::WINDOW_BUTTON_SELF_UPDATE, "Self-update");
		_advancedSizer->Add(button, 0, wxTOP | wxLEFT | wxBOTTOM, 20);
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
		_configurationSizer->Add(new wxStaticLine(), 0, wxTOP | wxBOTTOM, 5);
		// _configurationSizer->Add(repentogonSelectionSizer, 0, wxLEFT | wxRIGHT, 20);
		_configurationSizer->Add(new wxStaticLine(), 0, wxBOTTOM, 20);
	}

	void MainFrame::AddLaunchOptions() {
		wxStaticBox* launchModeBox = new wxStaticBox(_optionsBox, -1, "Launch Options");
		wxBoxSizer* launchModeBoxSizer = new wxBoxSizer(wxVERTICAL);
		launchModeBox->SetSizer(launchModeBoxSizer);

		wxSizer* box = new wxBoxSizer(wxHORIZONTAL);
		box->Add(new wxStaticText(launchModeBox, -1, "Launch mode: "));

		_launchMode = new wxComboBox(launchModeBox, WINDOW_COMBOBOX_LAUNCH_MODE);
		_repentogonLaunchModeIdx = _launchMode->Insert("Repentogon", 0, (void*)nullptr);
		_launchMode->Insert("Vanilla", 0, (void*)nullptr);
		_launchMode->SetValue("Repentogon");

		box->Add(_launchMode);

		launchModeBoxSizer->Add(box, 0, wxTOP | wxLEFT | wxRIGHT, 20);

		wxButton* launchButton = new wxButton(launchModeBox, WINDOW_BUTTON_LAUNCH_BUTTON, "Launch game");
		launchModeBoxSizer->Add(launchButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 20);

		wxButton* updateButton = new wxButton(launchModeBox, WINDOW_BUTTON_FORCE_UPDATE, "Update Repentogon (force, stable version)");
		launchModeBoxSizer->Add(updateButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 20);

		wxButton* unstableUpdateButton = new wxButton(launchModeBox, WINDOW_BUTTON_FORCE_UNSTABLE_UPDATE, "Update Repentogon (force, unstable version)");
		launchModeBoxSizer->Add(unstableUpdateButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 20);

		launchModeBoxSizer->Add(new wxStaticLine(), 0, wxBOTTOM, 5);

		_optionsSizer->Add(launchModeBox, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 20);
	}

	void MainFrame::AddRepentogonOptions() {
		wxStaticBox* repentogonBox = new wxStaticBox(_optionsBox, -1, "REPENTOGON Options");
		wxBoxSizer* repentogonBoxSizer = new wxBoxSizer(wxVERTICAL);
		repentogonBox->SetSizer(repentogonBoxSizer);

		// wxCheckBox* updates = new wxCheckBox(this, WINDOW_CHECKBOX_REPENTOGON_UPDATES, "Check for updates");
		// updates->SetValue(true);
		wxCheckBox* console = new wxCheckBox(repentogonBox, WINDOW_CHECKBOX_REPENTOGON_CONSOLE, "Enable console window");
		console->SetValue(false);

		// _updates = updates;
		_console = console;

		// _optionsGrid->Add(updates, wxGBPosition(1, 0));
		repentogonBoxSizer->Add(console, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 20);

		_repentogonOptions = repentogonBox;
		_optionsSizer->Add(repentogonBox, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 20);
	}

	void MainFrame::AddVanillaOptions() {
		wxStaticBox* vanillaBox = new wxStaticBox(_optionsBox, -1, "Vanilla Options");
		wxBoxSizer* vanillaBoxSizer = new wxBoxSizer(wxVERTICAL);
		vanillaBox->SetSizer(vanillaBoxSizer);

		wxSizer* levelSelectSizer = new wxBoxSizer(wxHORIZONTAL);
		levelSelectSizer->Add(new wxStaticText(vanillaBox, -1, "Starting stage: "));
		_levelSelect = CreateLevelsComboBox(vanillaBox);
		levelSelectSizer->Add(_levelSelect);

		vanillaBoxSizer->Add(levelSelectSizer, 0, wxTOP | wxLEFT | wxRIGHT, 20);
		_luaDebug = new wxCheckBox(vanillaBox, WINDOW_CHECKBOX_VANILLA_LUADEBUG, "Enable luadebug (unsafe)");
		vanillaBoxSizer->Add(_luaDebug, 0, wxLEFT | wxRIGHT, 20);
		vanillaBoxSizer->Add(new wxStaticLine(), 0, wxTOP | wxBOTTOM, 5);

		wxSizer* heapSizeBox = new wxBoxSizer(wxHORIZONTAL);
		wxTextValidator heapSizeValidator(wxFILTER_NUMERIC);
		wxTextCtrl* heapSizeCtrl = new wxTextCtrl(vanillaBox, WINDOW_TEXT_VANILLA_LUAHEAPSIZE, "1024");
		heapSizeCtrl->SetValidator(heapSizeValidator);
		_luaHeapSize = heapSizeCtrl;
		wxStaticText* heapSizeText = new wxStaticText(vanillaBox, -1, "Lua heap size (MB): ");
		heapSizeBox->Add(heapSizeText);
		heapSizeBox->Add(heapSizeCtrl);
		vanillaBoxSizer->Add(heapSizeBox, 0, wxLEFT | wxRIGHT, 20);
		vanillaBoxSizer->Add(new wxStaticLine(), 0, wxBOTTOM, 5);

		_optionsSizer->Add(vanillaBox, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 20);
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
		}
		else {
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
		}
		else if (!strcmp(text, "--")) {
			_options.levelStage = _options.stageType = 0;
		}
	}

	void MainFrame::OnLauchModeSelect(wxCommandEvent& event) {
		wxComboBox* box = dynamic_cast<wxComboBox*>(event.GetEventObject());
		if (box->GetValue() == "Vanilla") {
			_options.mode = LAUNCH_MODE_VANILLA;
		}
		else {
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
			// _options.updates = box->GetValue();
			break;

		case WINDOW_CHECKBOX_VANILLA_LUADEBUG:
			_options.luaDebug = box->GetValue();
			break;

		default:
			return;
		}
	}

	void MainFrame::OnSelfUpdateClick(wxCommandEvent& event) {
		Log("Performing self-update (forcibly triggered)");
		Launcher::SelfUpdateErrorCode result = _installationManager._selfUpdater.DoSelfUpdate(_options.unstableUpdates, true);
		HandleSelfUpdateResult(result);
	}

	void MainFrame::HandleSelfUpdateResult(SelfUpdateErrorCode const& updateResult) {
		InstallationManager::SelfUpdateResult result = _installationManager.HandleSelfUpdateResult(updateResult);
		if (result == InstallationManager::SELF_UPDATE_PARTIAL) {
			bool ok = true;
			int currentRetry = 1;
			int maxRetries = 3;
			do {
				wxDialog promptRetry(this, -1, "Retry update", wxDefaultPosition, wxDefaultSize, wxYES_NO, "Retry update ?");
				int modalResult = promptRetry.ShowModal();
				if (modalResult == 0) {
					break;
				}
				ok = _installationManager.ResumeSelfUpdate(currentRetry, maxRetries);
				++currentRetry;
			} while (ok && currentRetry <= maxRetries);
		}
	}

	void MainFrame::Launch(wxCommandEvent& event) {
		Log("Launching the game with the following options:");
		Log("\tRepentogon:");
		if (_options.mode == LAUNCH_MODE_VANILLA) {
			Log("\t\tRepentogon is disabled");
		}
		else {
			Log("\t\tRepentogon is enabled");
			Log("\t\tEnable Repentogon console window: %s", _options.console ? "yes" : "no");
		}
		Log("\tVanilla:");
		if (_options.levelStage) {
			Log("\t\tStarting stage: %d.%d", _options.levelStage, _options.stageType);
		}
		else {
			Log("\t\tStarting stage: not selected");
		}
		Log("\t\tLua debug: %s", _options.luaDebug ? "yes" : "no");
		Log("\t\tLua heap size: %dM", _options.luaHeapSize);

		_options.WriteConfiguration(this, _installationManager._installation);
		::Launch(_options);
	}

	void MainFrame::InitializeIsaacFolderPath(bool needIsaacFolder, bool canWriteConfiguration) {
		if (needIsaacFolder || !_installationManager.CheckIsaacInstallation()) {
			std::string path = PromptIsaacInstallation();
			std::filesystem::path p(path);
			p.remove_filename();
			if (!_installationManager._installation.SetIsaacInstallationFolder(p.string())) {
				LogError("Unable to configure the Isaac installation folder");
				return;
			}
			else {
				Log("Set Isaac installation folder to %s", p.string().c_str());
			}

			OnFileSelected(path, NoIsaacColor, _isaacFileText, NoIsaacText);
		}
		else {
			OnFileSelected(_installationManager._installation.GetIsaacInstallationFolder() + "isaac.ng.exe", 
				NoIsaacColor, _isaacFileText, NoIsaacText);
		}
	}

	void MainFrame::HandleLauncherUpdates(bool allowDrafts) {
		InstallationManager::CheckSelfUpdateResult result = _installationManager.CheckSelfUpdates(allowDrafts);
		if (result.code == InstallationManager::SELF_UPDATE_CHECK_NEW) {
			if (PromptLauncherUpdate(result.version, result.url)) {
				Log("Initiating update");
				DoSelfUpdate(result.version, result.url);
			}
			else {
				Log("Skipping self-updater as per user choice");
			}
		}
	}

	void MainFrame::PostInit() {
		Log("Welcome to the REPENTOGON Launcher (version %s)", Launcher::version);
		std::string currentDir = Filesystem::GetCurrentDirectory_();
		Log("Current directory is: %s", currentDir.c_str());
		
		HandleLauncherUpdates(true);

		LPSTR cli = GetCommandLineA();
		Log("Command line: %s", cli);
		Log("Loading Repentogon configuration...");

		
		bool needIsaacFolder = true;
		bool canWriteConfiguration = false;

		_installationManager.InitFolders(&needIsaacFolder, &canWriteConfiguration);
		InitializeIsaacFolderPath(needIsaacFolder, canWriteConfiguration);

		InitializeOptions();

		if (!_installationManager.CheckIsaacVersion()) {
			if (_repentogonLaunchModeIdx != -1) {
				_launchMode->Delete(_repentogonLaunchModeIdx);
			}
			return;
		}

		auto installationState = _installationManager.CheckRepentogonInstallation(false, false);
		bool checkUpdates = (installationState != InstallationManager::REPENTOGON_INSTALLATION_CHECK_LEGACY);
		if (installationState == InstallationManager::REPENTOGON_INSTALLATION_CHECK_KO) {
			if (PromptRepentogonInstallation()) {
				_installationManager.InstallLatestRepentogon(true, _options.unstableUpdates);
				_installationManager.CheckRepentogonInstallation(true, false);
			}

			checkUpdates = false;
		}

		if (checkUpdates && _options.update) {
			Log("Checking for Repentogon updates...");
			rapidjson::Document release;
			if (!_installationManager.CheckRepentogonUpdates(release, _options.unstableUpdates, false)) {
				Log("Repentogon is up-to-date");
			}
			else {
				Log("An update is available. Updating Repentogon...");
				_installationManager.InstallRepentogon(release);
				_installationManager.CheckRepentogonInstallation(false, false);
			}
		}
	}

	void MainFrame::DoSelfUpdate(std::string const& version, std::string const& url) {
		SelfUpdateErrorCode result = _installationManager._selfUpdater.DoSelfUpdate(version, url);
		HandleSelfUpdateResult(result);
	}

	bool MainFrame::PromptRepentogonInstallation() {
		wxMessageDialog dialog(this, "No valid REPENTOGON installation found.\nDo you want to install now ?", "REPENTOGON Installation", wxYES_NO | wxCANCEL);
		int result = dialog.ShowModal();
		return result == wxID_OK || result == wxID_YES;
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
		std::string configurationFile = _installationManager._installation.GetLauncherConfigurationPath();
		char readerMem[sizeof(INIReader)];

		bool invokeReader = false;
		if (!configurationFile.empty() && Filesystem::FileExists(configurationFile.c_str())) {
			new (readerMem) INIReader(configurationFile);
			invokeReader = true;
		}

		INIReader* reader = reinterpret_cast<INIReader*>(readerMem);
		if (!invokeReader || reader->ParseError() == -1) {
			if (!invokeReader) {
				LogError("No configuration file found, using defaults");
			}
			else {
				LogError("Malformed configuration file found (%s), using defaults", configurationFile.c_str());
			}

			wxMessageDialog dialog(this, "Do you want to have the launcher automatically update Repentogon?\n(Selecting \"Yes\" will immediately update Repentogon to the latest versions)", "Automatic Repentogon updates", wxYES_NO | wxCANCEL);
			int result = dialog.ShowModal();

			wxMessageDialog unstableDialog(this, "Do you want to download unstable updates ?\n(If you are not a modder, you probably don't want this)", "Unstable Repentogon updates", wxYES_NO | wxCANCEL);
			int unstableResult = unstableDialog.ShowModal();

			_options.InitializeDefaults(this, result == wxID_OK || result == wxID_YES,
				result == wxID_OK || result == wxID_YES, 
				_installationManager.IsValidRepentogonInstallation(true));
		}
		else {
			Log("Found configuration file launcher.ini");
			_options.InitializeFromConfig(this, *reader,
				_installationManager.IsValidRepentogonInstallation(true));
		}
		
		InitializeGUIFromOptions();
	}

	void MainFrame::InitializeGUIFromOptions() {
		_console->SetValue(_options.console);

		// stringstream sucks
		char buffer[11];
		sprintf(buffer, "%d", _options.luaHeapSize);
		_luaHeapSize->SetValue(wxString(buffer));

		_luaDebug->SetValue(_options.luaDebug);
		InitializeLevelSelectFromOptions();
		if (_options.mode == LAUNCH_MODE_REPENTOGON) {
			_launchMode->SetValue("Repentogon");
		}
		else {
			_launchMode->SetValue("Vanilla");
		}

		UpdateRepentogonOptionsFromLaunchMode();
	}

	void MainFrame::InitializeLevelSelectFromOptions() {
		int level = _options.levelStage, type = _options.stageType;
		std::string value;
		if (level == 0) {
			value = "--";
		}
		else {
			std::string levelName;
			if (level <= IsaacInterface::STAGE4_2) {
				/* To find the level name, divide level by two to get the chapter - 1, then add the stage type. */
				levelName = IsaacInterface::levelNames[((level - 1) / 2) * 5 + type];
				if (level % 2 == 0) {
					levelName += " II";
				}
				else {
					levelName += " I";
				}

				char buffer[10];
				sprintf(buffer, " (%d.%d)", level, type);
				levelName += std::string(buffer);
			}
			else {
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
		}
		else {
			_repentogonOptions->Enable(false);
			// _console->Enable(false);
		}
	}

	void MainFrame::ForceUpdate(wxCommandEvent& event) {
		Log("Forcibly updating Repentogon to the latest version");
		if (_installationManager.IsLegacyRepentogonInstallation()) {
			if (!_installationManager.UninstallLegacyRepentogon() && 
				Filesystem::FileExists((_installationManager._installation.GetIsaacInstallationFolder() + "/dsound.dll").c_str())) {
				LogWarn("[Force update] Identified a legacy Repentogon installation, but couldn't remove dsound.dll. Repentogon may not work after the update");
			}
		}

		wxButton* source = dynamic_cast<wxButton*>(event.GetEventObject());
		_installationManager.InstallLatestRepentogon(true, source->GetId() == WINDOW_BUTTON_FORCE_UNSTABLE_UPDATE);
		_installationManager.CheckRepentogonInstallation(false, true);
	}

	Launcher::fs::ConfigurationFileLocation MainFrame::PromptConfigurationFileLocation() {
		wxString message("The launcher was not able to find a configuration file. If this is your first run, it is normal.\n");
		std::string saveFolder = _installationManager._installation.GetSaveFolder();
		if (saveFolder.empty()) {
			message += "The launcher was not able to find your user profile directory. As such, the configuration fille will be created next to the launcher.";
			wxMessageDialog dialog(this, message, "Information", wxOK);
			dialog.ShowModal();
			return Launcher::fs::CONFIG_FILE_LOCATION_HERE;
		}
		else {
			message += "The launcher will proceed to create its configuration file.\n\n"
				"You can decide whether you want that file to be installed next to the launcher, or in the same folder as the Repentance save files\n"
				"The launcher identified your Repentance save folder as: ";
			message += _installationManager._installation.GetSaveFolder();
			wxString options[] = {
				"Install next to launcher",
				"Install in Repentance save folder"
			};
			wxSingleChoiceDialog dialog(this, message, "Create the launcher's configuration file", 2, options);
			dialog.ShowModal();

			if (dialog.GetSelection() == 0) {
				return Launcher::fs::CONFIG_FILE_LOCATION_HERE;
			}
			else {
				return Launcher::fs::CONFIG_FILE_LOCATION_SAVE;
			}
		}

		return Launcher::fs::CONFIG_FILE_LOCATION_HERE;
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
		sizer->Add(new wxStaticText(_configurationBox, -1, intro), 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 20);
		wxButton* isaacSelectionButton = new wxButton(_configurationBox, windowId, buttonText);
		sizer->Add(isaacSelectionButton, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);
		wxTextCtrl* textCtrl = new wxTextCtrl(_configurationBox, -1, wxEmptyString, wxDefaultPosition, wxSize(400, -1), wxTE_READONLY | wxTE_RICH);
		textCtrl->SetBackgroundColour(*wxWHITE);

		wxTextAttr textAttr = textCtrl->GetDefaultStyle();
		textCtrl->SetDefaultStyle(wxTextAttr(emptyColor));
		textCtrl->AppendText(emptyText);
		textCtrl->SetDefaultStyle(textAttr);

		sizer->Add(textCtrl, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);

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
}