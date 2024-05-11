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
#include "launcher/window.h"
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
EVT_BUTTON(Launcher::WINDOW_BUTTON_SELECT_ISAAC, Launcher::MainFrame::OnIsaacSelectClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_SELF_UPDATE, Launcher::MainFrame::OnSelfUpdateClick)
wxEND_EVENT_TABLE()

namespace Launcher {
	static std::tuple<wxFont, wxFont> MakeBoldFont(wxWindow* window);
	static wxComboBox* CreateLevelsComboBox(wxWindow* window);

	// Read chunks of 1MB in the zip stream of REPENTOGON.zip
	static constexpr size_t StreamChunkSize = 1 << 20; 

	namespace Defaults {
		bool console = false;
		int levelStage = 0;
		int stageType = 0;
		bool luaDebug = false;
		int luaHeapSize = 1024;
		bool update = true;
	}

	namespace Sections {
		std::string repentogon("Repentogon");
		std::string vanilla("Vanilla");
		std::string shared("Shared");
	}

	namespace Keys {
		std::string console("Console");
		std::string levelStage("LevelStage");
		std::string luaDebug("LuaDebug");
		std::string luaHeapSize("LuaHeapSize");
		std::string stageType("StageType");
		std::string launchMode("LaunchMode");
		std::string update("Update");
	}

	namespace IsaacInterface {
		// LevelStage
		static constexpr unsigned int STAGE_NULL = 0;
		static constexpr unsigned int STAGE4_1 = 7;
		static constexpr unsigned int STAGE4_2 = 8;
		static constexpr unsigned int STAGE4_3 = 9;
		static constexpr unsigned int STAGE8 = 13;
		static constexpr unsigned int NUM_STAGES = 14;

		// StageType
		static constexpr unsigned int STAGETYPE_ORIGINAL = 0;
		static constexpr unsigned int STAGETYPE_GREEDMODE = 3;
		static constexpr unsigned int STAGETYPE_REPENTANCE_B = 5;
	}

	const char* levelNames[] = {
			"Basement",
			"Cellar",
			"Burning Basement",
			"Downpour",
			"Dross",
			"Caves",
			"Catacombs",
			"Flooded Caves",
			"Mines",
			"Ashpit",
			"Depths",
			"Necropolis",
			"Dank Depths",
			"Mausoleum",
			"Gehenna",
			"Womb",
			"Utero",
			"Scarred Womb",
			"Corpse",
			NULL
	};

	static const char* uniqueLevelNames[] = {
		"??? (9.0)",
		"Sheol (10.0)",
		"Cathedral (10.1)",
		"Dark Room (11.0)",
		"Chest (11.1)",
		"The Void (12.0)",
		"Home (13.0)",
		NULL
	};

	MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "REPENTOGON Launcher") {
		memset(&_options, 0, sizeof(_options));
		// _optionsGrid = new wxGridBagSizer(0, 20);
		_console = nullptr;
		_luaHeapSize = nullptr;
		_hasRepentogon = false;
		_hasIsaac = false;
		_repentogonVersion = nullptr;

		SetSize(1024, 650);

		wxBoxSizer* optionsSizer = new wxBoxSizer(wxHORIZONTAL);
		wxStaticBox* optionsBox = new wxStaticBox(this, -1, "Game configuration");
		optionsBox->SetSizer(optionsSizer);

		wxSizer* verticalSizer = new wxBoxSizer(wxVERTICAL);
		wxTextCtrl* logWindow = new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, 400), wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH);
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

	void MainFrame::AddAdvancedOptions() {
		wxButton* button = new wxButton(_advancedOptions, Launcher::WINDOW_BUTTON_SELF_UPDATE, "Self-update");
		_advancedSizer->Add(button, 0, wxTOP | wxLEFT | wxBOTTOM, 20);
	}

	void MainFrame::AddLauncherConfigurationOptions() {
		wxBoxSizer* isaacSelectionSizer = new wxBoxSizer(wxHORIZONTAL);
		AddLauncherConfigurationTextField("Indicate the path of the isaac-ng.exe file",
			"Select file", "No file specified, won't be able to launch anything", 
			*wxRED, isaacSelectionSizer, &_isaacFileText);

		wxBoxSizer* repentogonSelectionSizer = new wxBoxSizer(wxHORIZONTAL);
		AddLauncherConfigurationTextField("Indicate folder in which to download and install REPENTOGON",
			"Select folder", 
			"No folder specified, will download in current folder", wxColor(255, 128, 64),
			repentogonSelectionSizer, &_repentogonInstallFolderText);

		// isaacSelectionSizer->Add(new wxStaticLine(), 0, wxBOTTOM | wxTOP, 20);

		_configurationSizer->Add(isaacSelectionSizer, 0, wxTOP | wxLEFT | wxRIGHT, 20);
		_configurationSizer->Add(new wxStaticLine(), 0, wxTOP | wxBOTTOM, 5);
		_configurationSizer->Add(repentogonSelectionSizer, 0, wxLEFT | wxRIGHT, 20);
		_configurationSizer->Add(new wxStaticLine(), 0, wxBOTTOM, 20);
	}

	void MainFrame::AddLaunchOptions() {
		wxStaticBox* launchModeBox = new wxStaticBox(_optionsBox, -1, "Launch Options");
		wxBoxSizer* launchModeBoxSizer = new wxBoxSizer(wxVERTICAL);
		launchModeBox->SetSizer(launchModeBoxSizer);

		wxSizer* box = new wxBoxSizer(wxHORIZONTAL);
		box->Add(new wxStaticText(launchModeBox, -1, "Launch mode: "));

		_launchMode = new wxComboBox(launchModeBox, WINDOW_COMBOBOX_LAUNCH_MODE);
		_launchMode->Insert("Repentogon", 0, (void*)nullptr);
		_launchMode->Insert("Vanilla", 0, (void*)nullptr);
		_launchMode->SetValue("Repentogon");

		box->Add(_launchMode);

		launchModeBoxSizer->Add(box, 0, wxTOP | wxLEFT | wxRIGHT, 20);

		wxButton* launchButton = new wxButton(launchModeBox, WINDOW_BUTTON_LAUNCH_BUTTON, "Launch game");
		launchModeBoxSizer->Add(launchButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 20);

		wxButton* updateButton = new wxButton(launchModeBox, WINDOW_BUTTON_FORCE_UPDATE, "Update Repentogon (force)");
		launchModeBoxSizer->Add(updateButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 20);
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
		wxFileDialog dialog(this, "Please select the isaac-ng.exe to launch", wxEmptyString, wxEmptyString, "isaac-ng.exe", wxFD_FILE_MUST_EXIST, wxDefaultPosition,
			wxDefaultSize, "Select isaac-ng.exe executable");
		dialog.ShowModal();
		std::string path = dialog.GetPath().ToStdString();
		if (!path.empty()) {
			_isaacFileText->SetValue(path);
		}
		else {
			wxTextAttr textAttr = _isaacFileText->GetDefaultStyle();
			_isaacFileText->Clear();
			_isaacFileText->SetDefaultStyle(wxTextAttr(*wxRED));
			_isaacFileText->AppendText("No file specified, won't be able to launch anything");
			_isaacFileText->SetDefaultStyle(textAttr);
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
		HRSRC updater = FindResource(NULL, MAKEINTRESOURCE(IDB_EMBEDEXE1), RT_RCDATA);
		if (!updater) {
			LogError("Unable to self update: resource not found in executable !");
			return;
		}

		HGLOBAL global = LoadResource(NULL, updater);
		if (!global) {
			LogError("Unable to load self updater: last error = %d !", GetLastError());
			return;
		}

		DWORD size = SizeofResource(NULL, updater);
		if (size == 0) {
			LogError("Invalid size of self updater (0) !");
			return;
		}

		void* data = LockResource(global);
		if (!data) {
			LogError("Unable to lock self updater !");
			return;
		}

		const char* filename = "./repentogon_launcher_self_updater.exe";
		FILE* output = fopen(filename, "wb");
		if (!output) {
			LogError("Unable to open temporary file %s to extract the self updater");
			return;
		}

		size_t count = fwrite(data, size, 1, output);
		if (count != size) {
			LogWarn("Inconsistent amount of data written: expected %d, wrote %llu", size, count);
		}

		fclose(output);

		std::string updateStatePath = _installation.GetSaveFolder() + "/repentogon_launcher_self_updater_state";
		FILE* updateState = fopen(updateStatePath.c_str(), "wb");
		if (!updateState) {
			LogError("Unable to open lock file for self update process");
			return;
		}

		fprintf(updateState, "%d", ::Updater::UpdateState::UPDATE_STATE_INIT);
		fflush(updateState);
		fclose(updateState);

		char cli[4096] = { 0 };
		int printfCount = snprintf(cli, 4096, "--lock-file=%s", updateStatePath.c_str());
		if (printfCount < 0) {
			LogError("Unable to generate command line for self updater");
			return;
		}

		PROCESS_INFORMATION info;
		memset(&info, 0, sizeof(info));

		STARTUPINFOA startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));

		BOOL ok = CreateProcessA(filename, cli, NULL, NULL, false, 0, NULL, NULL, &startupInfo, &info);
		if (!ok) {
			LogError("Unable to launch self updater");
		}
		else {
			Log("Launched self updater");
		}
		HANDLE child = OpenProcess(SYNCHRONIZE, false, info.dwProcessId);

		LogNoNL("Waiting until self updater is ready... ");
		WaitForInputIdle(child, INFINITE);
		Log("Done");

		ExitProcess(0);
	}

	void MainFrame::Launch(wxCommandEvent& event) {
		Log("Launching the game with the following options:");
		Log("\tRepentogon:");
		if (_options.mode == LAUNCH_MODE_VANILLA) {
			Log("\t\tRepentogon is disabled");
		}
		else {
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

		WriteLauncherConfiguration();
		::Launch(_options);
	}

	void MainFrame::PostInit() {
		Log("Welcome to the REPENTOGON Launcher version %s", Launcher::version);
		Log("Loading configuration...");

		Launcher::fs::IsaacInstallationPathInitResult initState = _installation.InitFolders();
		bool needConfigurationFileInput = false;
		switch (initState) {
		case Launcher::fs::INSTALLATION_PATH_INIT_ERR_PROFILE_DIR:
			LogError("No configuration file found in current dir and unable to access the Repentance save folder");
			needConfigurationFileInput = true;
			break;

		case Launcher::fs::INSTALLATION_PATH_INIT_ERR_NO_SAVE_DIR:
			LogError("No configuration file found in current dir and no Repentance save folder found");
			needConfigurationFileInput = true;
			break;

		case Launcher::fs::INSTALLATION_PATH_INIT_ERR_NO_CONFIG:
			LogError("No configuration file found in current dir and none found in the Repentance save folder");
			needConfigurationFileInput = true;
			break;

		case Launcher::fs::INSTALLATION_PATH_INIT_ERR_OPEN_CONFIG:
			LogError("Error while opening configuration file %s", _installation.GetLauncherConfigurationPath().c_str());
			needConfigurationFileInput = true;
			break;

		case Launcher::fs::INSTALLATION_PATH_INIT_ERR_PARSE_CONFIG:
			LogError("Error while processing configuration file %s, syntax error on line %d\n",
				_installation.GetLauncherConfigurationPath().c_str(),
				_installation.GetConfigurationFileSyntaxErrorLine());
			needConfigurationFileInput = true;
			break;

		default:
			break;
		}
		
		if (!needConfigurationFileInput) {
			Log("Found configuration file %s", _installation.GetLauncherConfigurationPath().c_str());
		}
		else {
			Launcher::fs::ConfigurationFileLocation location = PromptConfigurationFileLocation();
			_installation.SetLauncherConfigurationPath(location);
		}

		_hasIsaac = CheckIsaacInstallation();
		if (!_hasIsaac) {
			std::string path = PromptIsaacInstallation();
			std::filesystem::path p(path);
			p.remove_filename();
			if (!_installation.SetIsaacInstallationFolder(p.string())) {
				LogError("Unable to configure the Isaac installation folder");
				return;
			}
		}

		if (!CheckIsaacVersion()) {
			return;
		}

		_hasRepentogon = CheckRepentogonInstallation();
		bool checkUpdates = true;
		if (!_hasRepentogon) {
			if (PromptRepentogonInstallation()) {
				DownloadAndInstallRepentogon(true);
				checkUpdates = false;
				_hasRepentogon = CheckRepentogonInstallation();
			}
			else {
				checkUpdates = false;
			}
		}

		InitializeOptions();

		rapidjson::Document launcherResponse;
		if (_updater.CheckLauncherUpdates(launcherResponse)) {
			DoSelfUpdate(launcherResponse);
		}

		if (checkUpdates && _options.update) {
			DownloadAndInstallRepentogon(false);
		}
	}

	bool MainFrame::PromptRepentogonInstallation() {
		wxMessageDialog dialog(this, "No valid REPENTOGON installation found.\nDo you want to install now ?", "REPENTOGON Installation", wxYES_NO | wxCANCEL);
		int result = dialog.ShowModal();
		return result == wxID_OK || result == wxID_YES;
	}

	bool MainFrame::CheckIsaacInstallation() {
		Log("Checking Isaac installation...");
		if (!_installation.CheckIsaacInstallation()) {
			LogError("Unable to find isaac-ng.exe");
			return false;
		}

		return true;
	}

	bool MainFrame::CheckIsaacVersion() {
		Log("Checking Isaac version...");
		fs::Version const* version = _installation.GetIsaacVersion();
		if (!version) {
			LogError("Unknown Isaac version. REPENTOGON will not launch.");
			return false;
		}

		Log("Identified Isaac version %s: ", version->version);
		if (!version->valid) {
			LogError("this version of the game does not support REPENTOGON.");
		}
		else {
			Log("this version of the game is compatible with REPENTOGON.");
		}

		return version->valid;
	}

	bool MainFrame::CheckRepentogonInstallation() {
		Log("Checking Repentogon installation...");
		if (_installation.CheckRepentogonInstallation()) {
			Log("Found a valid Repentogon installation: ");
			Log("\tZHL version: %s", _installation.GetZHLVersion().c_str());
			Log("\tRepentogon version: %s", _installation.GetRepentogonVersion().c_str());

			if (_installation.GetRepentogonInstallationState() == fs::REPENTOGON_INSTALLATION_STATE_LEGACY) {
				LogWarn("\tFound legacy dsound.dll");

			}

			return true;
		}
		else {
			Log("Found no valid installation of Repentogon: ");
			std::vector<fs::FoundFile> const& files = _installation.GetRepentogonInstallationFilesState();
			for (fs::FoundFile const& file : files) {
				if (file.found) {
					Log("\t%s: found", file.filename.c_str());
				}
				else {
					Log("\t%s: not found", file.filename.c_str());
				}
			}

			bool zhlVersionAvailable = false, repentogonVersionAvailable = false;
			if (_installation.WasLibraryLoaded(fs::LOADABLE_DLL_ZHL)) {
				std::string const& zhlVersion = _installation.GetZHLVersion();
				if (zhlVersion.empty()) {
					Log("\tlibzhl.dll: unable to find version");
				}
				else {
					zhlVersionAvailable = true;
					Log("\tlibzhl.dll: found version %s", zhlVersion.c_str());
				}
			}
			else {
				Log("\tlibzhl.dll: Unable to load");
			}

			if (_installation.WasLibraryLoaded(fs::LOADABLE_DLL_REPENTOGON)) {
				std::string const& repentogonVersion = _installation.GetRepentogonVersion();
				if (repentogonVersion.empty()) {
					Log("\tzhlRepentogon.dll: unable to find version");
				}
				else {
					repentogonVersionAvailable = true;
					Log("\tzhlRepentogon.dll: found version %s", repentogonVersion.c_str());
				}
			}
			else {
				Log("\tzhlRepentogon.dll: Unable to load");
			}

			if (zhlVersionAvailable && repentogonVersionAvailable) {
				if (_installation.RepentogonZHLVersionMatch()) {
					Log("\tZHL / Repentogon version match");
				}
				else {
					Log("\tZHL / Repentogon version mismatch");
				}
			}

			return false;
		}
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
		for (const char* name : levelNames) {
			if (!name)
				continue;

			wxString s;
			int level = 1 + 2 * (variant / 5);
			box->Insert(s.Format("%s I (%d.%d)", name, level, variant % 5), pos++, (void*)nullptr);
			box->Insert(s.Format("%s II (%d.%d)", name, level + 1, variant % 5), pos++, (void*)nullptr);

			++variant;
		}

		for (const char* name : uniqueLevelNames) {
			if (!name)
				continue;
			box->Insert(wxString(name), pos++, (void*)nullptr);
		}

		return box;
	}

	void MainFrame::InitializeOptions() {
		INIReader reader(std::string("launcher.ini"));
		if (reader.ParseError() == -1) {
			Log("No configuration file found, using defaults");
			_options.console = Defaults::console;
			_options.levelStage = Defaults::levelStage;
			_options.luaDebug = Defaults::luaDebug;
			_options.luaHeapSize = Defaults::luaHeapSize;
			_options.mode = _hasRepentogon ? LAUNCH_MODE_REPENTOGON : LAUNCH_MODE_VANILLA;
			_options.stageType = Defaults::stageType;
			// _options.update = Defaults::update;

			wxMessageDialog dialog(this, "Do you want to have the launcher automatically update Repentogon?\n(Warning: this will be applied **immediately**)", "Auto-updates", wxYES_NO | wxCANCEL);
			int result = dialog.ShowModal();
			_options.update = (result == wxID_OK || result == wxID_YES);
		}
		else {
			Log("Found configuration file launcher.ini");
			_options.console = reader.GetBoolean(Sections::repentogon, Keys::console, Defaults::console);
			_options.levelStage = reader.GetInteger(Sections::vanilla, Keys::levelStage, Defaults::levelStage);
			_options.luaDebug = reader.GetBoolean(Sections::vanilla, Keys::luaDebug, Defaults::luaDebug);
			_options.luaHeapSize = reader.GetInteger(Sections::vanilla, Keys::luaHeapSize, Defaults::luaHeapSize);
			_options.stageType = reader.GetInteger(Sections::vanilla, Keys::stageType, Defaults::stageType);
			_options.mode = (LaunchMode)reader.GetInteger(Sections::shared, Keys::launchMode, LAUNCH_MODE_REPENTOGON);
			_options.update = reader.GetBoolean(Sections::repentogon, Keys::update, Defaults::update);

			if (_options.mode != LAUNCH_MODE_REPENTOGON && _options.mode != LAUNCH_MODE_VANILLA) {
				LogWarn("Invalid value %d for %s field in launcher.ini. Overriding with default", _options.mode, Keys::launchMode);
				if (_hasRepentogon) {
					_options.mode = LAUNCH_MODE_REPENTOGON;
				}
				else {
					_options.mode = LAUNCH_MODE_VANILLA;
				}
			}

			if (_options.luaHeapSize < 0) {
				LogWarn("Invalid value %d for %s field in launcher.ini. Overriding with default", _options.luaHeapSize, Keys::luaHeapSize.c_str());
				_options.luaHeapSize = Defaults::luaHeapSize;
			}

			if (_options.levelStage < IsaacInterface::STAGE_NULL || _options.levelStage > IsaacInterface::STAGE8) {
				LogWarn("Invalid value %d for %s field in launcher.ini. Overriding with default", _options.levelStage, Keys::levelStage.c_str());
				_options.levelStage = Defaults::levelStage;
				_options.stageType = Defaults::stageType;
			}

			if (_options.stageType < IsaacInterface::STAGETYPE_ORIGINAL || _options.stageType > IsaacInterface::STAGETYPE_REPENTANCE_B) {
				LogWarn("Invalid value %d for %s field in launcher.ini. Overriding with default", _options.stageType, Keys::stageType.c_str());
				_options.stageType = Defaults::stageType;
			}

			if (_options.stageType == IsaacInterface::STAGETYPE_GREEDMODE) {
				LogWarn("Value 3 (Greed mode) for %s field in launcher.ini is deprecated since Repentance."
						"Overriding with default", Keys::stageType.c_str());
				_options.stageType = Defaults::stageType;
			}

			// Validate stage type for Chapter 4.
			if (_options.levelStage == IsaacInterface::STAGE4_1 || _options.levelStage == IsaacInterface::STAGE4_2) {
				if (_options.stageType == IsaacInterface::STAGETYPE_REPENTANCE_B) {
					LogWarn("Invalid value %d for %s field associated with value %d "
							"for %s field in launcher.ini. Overriding with default", _options.stageType, Keys::levelStage.c_str(),
							_options.stageType, Keys::stageType.c_str());
					_options.stageType = Defaults::stageType;
				}
			}

			// Validate stage type for Chapters > 4.
			if (_options.levelStage >= IsaacInterface::STAGE4_3 && _options.levelStage <= IsaacInterface::STAGE8) {
				if (_options.stageType != IsaacInterface::STAGETYPE_ORIGINAL) {
					LogWarn("Invalid value %d for %s field associated with value %d "
							"for %s field in launcher.ini. Overriding with default", _options.levelStage, Keys::levelStage.c_str(),
							_options.stageType, Keys::stageType.c_str());
					_options.stageType = Defaults::stageType;
				}
			}
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
				levelName = levelNames[((level - 1) / 2) * 5 + type];
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
				levelName = uniqueLevelNames[level - IsaacInterface::STAGE4_3];
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

	void MainFrame::WriteLauncherConfiguration() {
		FILE* f = fopen("launcher.ini", "w");
		if (!f) {
			LogError("Unable to open file to write launcher configuration, skpping");
			return;
		}

		fprintf(f, "[%s]\n", Sections::repentogon.c_str());
		fprintf(f, "%s = %d\n", Keys::console.c_str(), _options.console);
		fprintf(f, "%s = %d\n", Keys::update.c_str(), _options.update);

		fprintf(f, "[%s]\n", Sections::vanilla.c_str());
		fprintf(f, "%s = %d\n", Keys::levelStage.c_str(), _options.levelStage);
		fprintf(f, "%s = %d\n", Keys::stageType.c_str(), _options.stageType);
		fprintf(f, "%s = %d\n", Keys::luaHeapSize.c_str(), _options.luaHeapSize);
		fprintf(f, "%s = %d\n", Keys::luaDebug.c_str(), _options.luaDebug);
		
		fprintf(f, "[%s]\n", Sections::shared.c_str());
		fprintf(f, "%s = %d\n", Keys::launchMode.c_str(), _options.mode);

		fclose(f);
	}

	bool MainFrame::DoRepentogonUpdate(rapidjson::Document& response) {
		RepentogonUpdateResult result = _updater.UpdateRepentogon(response);
		RepentogonUpdateState const& state = _updater.GetRepentogonUpdateState();
		switch (result) {
		case REPENTOGON_UPDATE_RESULT_MISSING_ASSET:
			LogError("Could not install Repentogon: bad assets in release information\n");
			LogError("Found hash.txt: %s\n", state.hashOk ? "yes" : "no");
			LogError("Found REPENTOGON.zip: %s\n", state.zipOk ? "yes" : "no");
			break;

		case REPENTOGON_UPDATE_RESULT_DOWNLOAD_ERROR:
			LogError("Could not install Repentogon: download error\n");
			LogError("Downloaded hash.txt: %s\n", state.hashFile ? "yes" : "no");
			LogError("Downloaded REPENTOGON.zip: %s\n", state.zipFile ? "yes" : "no");
			break;

		case REPENTOGON_UPDATE_RESULT_BAD_HASH:
			LogError("Could not install Repentogon: bad archive hash\n");
			LogError("Expected hash \"%s\", got \"%s\"\n", state.hash.c_str(), state.zipHash.c_str());
			break;

		case REPENTOGON_UPDATE_RESULT_EXTRACT_FAILED:
		{
			int i = 0;
			LogError("Could not install Repentogon: error while extracting archive\n");
			for (auto const& [filename, success] : state.unzipedFiles) {
				if (filename.empty()) {
					LogError("Could not extract file %d from the archive\n", i);
				}
				else {
					LogError("Extracted %s: %s\n", filename.c_str(), success ? "yes" : "no");
				}
			}
			break;
		}

		case REPENTOGON_UPDATE_RESULT_OK:
			Log("Successfully installed latest Repentogon release\n");
			break;

		default:
			LogError("Unknown error code from Updater::UpdateRepentogon: %d\n", result);
		}

		return result == REPENTOGON_UPDATE_RESULT_OK;
	}

	bool MainFrame::DoSelfUpdate(rapidjson::Document& response) {
		return false;
	}

	void MainFrame::ForceUpdate(wxCommandEvent& event) { 
		DownloadAndInstallRepentogon(true);
	}

	void MainFrame::DownloadAndInstallRepentogon(bool force) {
		rapidjson::Document document;
		if (!DoCheckRepentogonUpdates(document, force)) {
			return;
		}

		DoRepentogonUpdate(document);
	}

	bool MainFrame::DoCheckRepentogonUpdates(rapidjson::Document& document, 
		bool force) {
		VersionCheckResult result = _updater.CheckRepentogonUpdates(document, _installation);
		if (result == VERSION_CHECK_NEW)
			return true;

		if (result == VERSION_CHECK_UTD)
			return force;

		return false;
	}

	Launcher::fs::ConfigurationFileLocation MainFrame::PromptConfigurationFileLocation() {
		wxString message("The launcher was not able to find a configuration file. If this is your first run, it is normal.\n");
		std::string saveFolder = _installation.GetSaveFolder();
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
			message += _installation.GetSaveFolder();
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
		wxBoxSizer* sizer, wxTextCtrl** result) {
		sizer->Add(new wxStaticText(_configurationBox, -1, intro), 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 20);
		wxButton* isaacSelectionButton = new wxButton(_configurationBox, Launcher::WINDOW_BUTTON_SELECT_ISAAC, buttonText);
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
}