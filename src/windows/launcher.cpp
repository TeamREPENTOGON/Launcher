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
#include "launcher/windows/advanced_options.h"
#include "launcher/configuration.h"
#include "launcher/installation.h"
#include "launcher/installation_manager.h"
#include "launcher/isaac.h"
#include "launcher/log_helpers.h"
#include "launcher/windows/launcher.h"
#include "launcher/self_update.h"
#include "launcher/version.h"
#include "shared/compat.h"
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
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES, Launcher::MainFrame::OnOptionSelected)
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
	
	MainFrame::MainFrame(Installation* installation) : wxFrame(nullptr, wxID_ANY, "REPENTOGON Launcher"),
		_logWindow(new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, -1),
			wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH)) {
		memset(&_options, 0, sizeof(_options));
		// _optionsGrid = new wxGridBagSizer(0, 20);
		_console = nullptr;
		_luaHeapSize = nullptr;

		wxStaticBox* optionsBox = new wxStaticBox(this, -1, "Game configuration");
		wxStaticBoxSizer* optionsSizer = new wxStaticBoxSizer(optionsBox, wxHORIZONTAL);

		wxSizer* verticalSizer = new wxBoxSizer(wxVERTICAL);
		wxTextCtrl* logWindow = _logWindow.Get();
		logWindow->SetBackgroundColour(*wxWHITE);

		wxStaticBox* configurationBox = new wxStaticBox(this, -1, "Launcher configuration");
		wxStaticBoxSizer* configurationSizer = new wxStaticBoxSizer(configurationBox, wxHORIZONTAL);

		wxSizerFlags logFlags = wxSizerFlags().Expand().Proportion(5).Border(wxLEFT | wxRIGHT | wxTOP);
		verticalSizer->Add(logWindow, logFlags);
		logFlags.Proportion(0);
		verticalSizer->Add(configurationSizer, logFlags);
		verticalSizer->Add(optionsSizer, logFlags);

		_configurationBox = configurationBox;
		_configurationSizer = configurationSizer;
		_optionsSizer = optionsSizer;
		_optionsBox = optionsBox;

		AddLauncherConfigurationOptions();
		AddRepentogonOptions();
		AddVanillaOptions();
		AddLaunchOptions();
		
		_advancedOptionsButton = new wxButton(this, WINDOW_BUTTON_ADVANCED_OPTIONS, "Advanced options...");
		wxSizerFlags advancedOptionsFlags = wxSizerFlags().Right();
		verticalSizer->Add(_advancedOptionsButton, advancedOptionsFlags);

		SetSizerAndFit(verticalSizer);

		SetBackgroundColour(wxColour(237, 237, 237));
	}

	MainFrame::~MainFrame() {
		_options.WriteConfiguration(&_logWindow, *_installation);
	}

	void MainFrame::AddLauncherConfigurationOptions() {
		wxBoxSizer* isaacSelectionSizer = new wxBoxSizer(wxHORIZONTAL);
		AddLauncherConfigurationTextField("Indicate the path of the Isaac executable file",
			"Select Isaac executable...", NoIsaacText, 
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

		_launchButton = new wxButton(launchModeBox, WINDOW_BUTTON_LAUNCH_BUTTON, "Launch game");
		_launchButton->Disable();
		launchModeBoxSizer->Add(_launchButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

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
		wxFileDialog dialog(this, "Please select the Binding of Isaac executable to launch", wxEmptyString, wxEmptyString, 
			wxEmptyString, wxFD_FILE_MUST_EXIST, wxDefaultPosition, wxDefaultSize, 
			"Select Binding of Isaac executable");
		dialog.ShowModal();
		HandleIsaacExecutableSelection(dialog.GetPath().ToStdString());
		
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
		_logWindow.Log("Launching the game with the following options:");
		_logWindow.Log("\tRepentogon:");
		if (_options.mode == LAUNCH_MODE_VANILLA) {
			_logWindow.Log("\t\tRepentogon is disabled");
		} else {
			_logWindow.Log("\t\tRepentogon is enabled");
			_logWindow.Log("\t\tEnable Repentogon console window: %s", _options.console ? "yes" : "no");
		}
		_logWindow.Log("\tVanilla:");
		if (_options.levelStage) {
			_logWindow.Log("\t\tStarting stage: %d.%d", _options.levelStage, _options.stageType);
		} else {
			_logWindow.Log("\t\tStarting stage: not selected");
		}
		_logWindow.Log("\t\tLua debug: %s", _options.luaDebug ? "yes" : "no");
		_logWindow.Log("\t\tLua heap size: %dM", _options.luaHeapSize);

		_options.WriteConfiguration(&_logWindow, *_installation);
		::Launcher::Launch(&_logWindow, _isaacFileText->GetValue().c_str().AsChar(), _installation->GetRepentogonInstallation().IsLegacy(), _options);
	}

	void MainFrame::EnableInterface(bool enable) {
		_logWindow.LogWarn("Interface freezing / unfreezing when launching the game is not yet implemented\n");
	}

	bool MainFrame::InitializeIsaacExecutablePath() {
		return HandleIsaacExecutableSelection(PromptIsaacInstallation());
	}

	bool MainFrame::SanityCheckLauncherUpdate() {
		return !Filesystem::FileExists(Comm::UnpackedArchiveName);
	}

	void MainFrame::SanitizeLauncherUpdate() {
		_logWindow.LogWarn("Found launcher update file %s in the current folder."
			"This may indicate a broken update. Deleting it.\n", Comm::UnpackedArchiveName);
		Filesystem::RemoveFile(Comm::UnpackedArchiveName);
	}

	void MainFrame::OneTimeIsaacPathInitialization() {
		_logWindow.Log("Attempting to autodetect launcher configuration and Isaac installation folder...");
		auto [isaacPath, repentogonOk] = _installation->Initialize();

		/* If isaacPath is not nullopt, all validation steps are good. There is 
		 * no need to use HandleIsaacExecutableSelection as that function
		 * should only be used for validation purposes.
		 */
		if (isaacPath) {
			_isaacFileText->SetValue(*isaacPath);
		} else {
			_logWindow.LogWarn("Could not load Isaac executable filepath from configuration and could not auto-detect it...\n");
			bool abortRequested = false;

			while (!abortRequested && !InitializeIsaacExecutablePath()) {
				wxMessageDialog dialog(this, "Invalid Isaac executable given, do you want to retry ? (Answering \"No\" will abort)", "Invalid folder", wxYES_NO);
				int result = dialog.ShowModal();
				abortRequested = (result == wxID_NO);
			}

			if (abortRequested) {
				Logger::Fatal("User did not provide a valid Isaac installation folder, aborting\n");
			}
		}
	}

	void MainFrame::PreInit() {
/* #ifdef LAUNCHER_UNSTABLE
		wxMessageBox("You are running an unstable version of the REPENTOGON launcher.\n"
			"If you wish to run a stable version, use the \"Self-update (stable version)\" button",
			"Unstable launcher version", wxCENTER | wxICON_WARNING);
		LogWarn("Running an unstable version of the launcher");
#endif */
		_logWindow.Log("Welcome to the REPENTOGON Launcher (version %s)", Launcher::version);
		std::string currentDir = Filesystem::GetCurrentDirectory_();
		_logWindow.Log("Current directory is: %s", currentDir.c_str());

		if (!SanityCheckLauncherUpdate()) {
			SanitizeLauncherUpdate();
		}

		LPSTR cli = GetCommandLineA();
		_logWindow.Log("Command line: %s", cli);

		OneTimeIsaacPathInitialization();		
		InitializeOptions();
		OneTimeInitRepentogon();
		UpdateRepentogonOptionsFromInstallation();
	}

	void MainFrame::OneTimeInitRepentogon() {
		/* We need to manage the following scenarios:
		 *	a) There is no Repentogon installation -> prompt
		 *  b) There is a Repentogon installation, but it is broken -> prompt
		 *  c) There is a Repentogon installation, but it's outdated -> prompt
		 *  d) There is a Repentogon installation, and it is up-to-date -> do nothing.
		 * 
		 * In cases a), b) and c) we need to check the result of the installation
		 * of Repentogon (if the user said yes). Cases a) and b) are technically
		 * the same.
		 * 
		 * Assertions: this function shall only be called if a valid Isaac
		 * installation has been found.
		 */

		if (!_installation->GetIsaacInstallation().IsValid()) {
			Logger::Fatal("MainFrame::OneTimeInitRepentogon called without a valid Isaac installation\n");
		}

		RepentogonInstallationStatus installationState = _installation->GetRepentogonInstallation().GetState();
		if (installationState == REPENTOGON_INSTALLATION_STATUS_NONE /* a) */ ||
			installationState == REPENTOGON_INSTALLATION_STATUS_BROKEN /* b) */) {
			/* RepentogonInstaller* window = new RepentogonInstaller(this);
			if (PromptRepentogonInstallation()) {
				RepentogonInstaller repentogonUpdateMgr(&_logWindow, _installation);
				repentogonUpdateMgr.InstallLatestRepentogon(true, _options.unstableUpdates);
			} */
		}

		bool needCheckRepentogonUpdates = true;
		bool successfullyInstalledRepentogon = false;
		bool attemptedToInstallRepentogon = false;
		bool attemptedToUpdateRepentogon = false;
		bool isLegacy = false;
		bool needToDisableRepentogonOptions = false;

		RepentogonInstaller repentogonUpdateMgr(_installation);
		_installation->CheckRepentogonInstallation();

		RepentogonInstallation const& repentogon = _installation->GetRepentogonInstallation();

		if (repentogon.GetState() == REPENTOGON_INSTALLATION_STATUS_NONE) {
			if (PromptRepentogonInstallation()) {
				attemptedToInstallRepentogon = true;
				repentogonUpdateMgr.InstallLatestRepentogon(true, _options.unstableUpdates);
				successfullyInstalledRepentogon = _installation->CheckRepentogonInstallation();
			}

			needCheckRepentogonUpdates = false;
		} else {
			isLegacy = repentogon.IsLegacy();
			// If an update occurs, this value is reassessed.
			needToDisableRepentogonOptions = isLegacy;
		}

		bool updateAttemptSucceeded = true;
		bool stillValidAfterUpdate;
		/* If installation is legacy, then an update is available _by construction_.
		 * Therefore there is no need to do a specific check.
		 */
		if (needCheckRepentogonUpdates && _options.update) {
			_logWindow.Log("Checking for Repentogon updates...");
			rapidjson::Document release;
			auto [checkUpdatesFuture, checkUpdatesMonitor] =
				repentogonUpdateMgr.CheckRepentogonUpdates(release, _options.unstableUpdates, false);
			Launcher::RepentogonInstaller::CheckRepentogonUpdatesResult checkUpdates = checkUpdatesFuture.get();
			if (checkUpdates == RepentogonInstaller::CHECK_REPENTOGON_UPDATES_UTD) {
				_logWindow.Log("Repentogon is up-to-date");
			} else if (checkUpdates == RepentogonInstaller::CHECK_REPENTOGON_UPDATES_NEW) {
				_logWindow.Log("An update is available. Updating Repentogon...");
				attemptedToInstallRepentogon = true;
				attemptedToUpdateRepentogon = true;

				auto [installFuture, installMonitor] = repentogonUpdateMgr.InstallRepentogon(release);
				if (!installFuture.get()) {
					updateAttemptSucceeded = false;
					_logWindow.LogError("An error occured while downloading the Repentogon update.\n");
				}

				stillValidAfterUpdate = _installation->CheckRepentogonInstallation();
				successfullyInstalledRepentogon = stillValidAfterUpdate && updateAttemptSucceeded;
			} else {
				_logWindow.LogError("Unable to check for availability of Repentogon updates\n");
				// No need to return, installation may be valid regardless
			}
		}
		
		if (attemptedToInstallRepentogon) {
			if (successfullyInstalledRepentogon) {
				if (repentogon.IsLegacy()) {
					_logWindow.Log("Legacy installation of Repentogon found. Repentogon will work, but the launcher cannot configure it.");
					needToDisableRepentogonOptions = true;
					isLegacy = true;
				} else {
					if (attemptedToUpdateRepentogon) {
						_logWindow.Log("Suscessfully updated Repentogon to version %s\n", repentogon.GetRepentogonVersion().c_str());
					} else {
						_logWindow.Log("Successfully installed Repentogon version %s\n", repentogon.GetRepentogonVersion().c_str());
					}

				}

				_logWindow.Log("State of the current Repentogon installation:\n");

				Launcher::DisplayRepentogonFilesVersion(_installation, 1, 
					attemptedToUpdateRepentogon && updateAttemptSucceeded,
					_logWindow.Get());
			} else {
				if (attemptedToUpdateRepentogon) {
					_logWindow.LogError("Unable to update Repentogon\n");
				} else {
					_logWindow.LogError("Unable to install Repentogon\n");
				}

				if (attemptedToUpdateRepentogon && stillValidAfterUpdate) {
					_logWindow.Log("Your previous installation of Repentogon is still valid and can be used\n");
				} else {
					_logWindow.Log("Information regarding what happened during the installation:\n");
					Launcher::DebugDumpBrokenRepentogonInstallation(_installation,
						_logWindow.Get());
					needToDisableRepentogonOptions = true;
				}
			}
		}

		if (needToDisableRepentogonOptions) {
			if (isLegacy) {
				_logWindow.LogWarn("Disabling Repentogon configuration options due to legacy installation\n");
			} else {
				_logWindow.LogWarn("Disabling Repentogon configuration options due to previous errors\n");
			}
		}

		UpdateRepentogonOptionsFromInstallation();
	}

	bool MainFrame::PromptRepentogonInstallation() {
		return PromptBoolean("No valid REPENTOGON installation found.\nDo you want to install now ?", "REPENTOGON Installation");
	}

	bool MainFrame::PromptAutomaticUpdates() {
		return PromptBoolean("Do you want the launcher to automatically keep Repentogon up-to-date ? (You can change your decision later)",
			"Automatic Repentogon updates");
	}

	bool MainFrame::PromptUnstableUpdates() {
		return PromptBoolean("Do you want the launcher to upgrade to unstable updates ? (You can change your decision later)",
			"Unstable updates");
	}

	bool MainFrame::PromptBoolean(wxString const& message, wxString const& shortMessage) {
		wxMessageDialog dialog(this, message, shortMessage, wxYES_NO | wxCANCEL);
		int result = dialog.ShowModal();
		return result == wxID_OK || result == wxID_YES;
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
		INIReader* reader = _installation->GetLauncherConfiguration()->GetReader();
		if (!reader) {
			wxMessageDialog dialog(this, "Do you want to have the launcher automatically update Repentogon?\n"
				"(Selecting \"Yes\" will immediately update Repentogon to the latest versions)", 
				"Automatic Repentogon updates", wxYES_NO | wxCANCEL);
			int result = dialog.ShowModal();

			wxMessageDialog unstableDialog(this, "Do you want to download unstable updates ?\n"
				"(If you are not a modder, you probably don't want this)",
				"Unstable Repentogon updates", wxYES_NO | wxCANCEL);
			int unstableResult = unstableDialog.ShowModal();

			_options.InitializeDefaults(&_logWindow,
				result == wxID_OK || result == wxID_YES,
				unstableResult == wxID_OK || unstableResult == wxID_YES, 
				_installation->GetRepentogonInstallation().IsValid(true));
		} else {
			_options.InitializeFromConfig(&_logWindow, *reader,
				_installation->GetRepentogonInstallation().IsValid(true));
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

	void MainFrame::UpdateRepentogonOptionsFromInstallation() {
		RepentogonInstallation const& repentogonInstallation = _installation->GetRepentogonInstallation();
		IsaacInstallation const& isaacInstallation = _installation->GetIsaacInstallation();

		if (!repentogonInstallation.IsValid(true) || !isaacInstallation.IsCompatibleWithRepentogon()) {
			if (_repentogonLaunchModeIdx != -1) {
				_launchMode->Delete(_repentogonLaunchModeIdx);
				_repentogonLaunchModeIdx = -1;
			}

			_launchMode->SetValue("Vanilla");
		} else {
			if (_repentogonLaunchModeIdx == -1) {
				_repentogonLaunchModeIdx = _launchMode->Append("Repentogon");
			}

			/* Intentionally not changing the selected launch mode value.
			 * 
			 * While the change in the above if branch is mandatory to prevent
			 * the game from incorrectly being injected with Repentogon in case
			 * of a legacy installation, or broken installation, changing the
			 * value here would override the value selected by the user the
			 * last time.
			 */
		}
	}

	void MainFrame::ForceRepentogonUpdate(bool allowPreReleases) {
		_logWindow.Log("Forcibly updating Repentogon to the latest version");

		RepentogonInstaller repentogonUpdateMgr(_installation);
		auto [future, monitor] = repentogonUpdateMgr.InstallLatestRepentogon(true, allowPreReleases);

		RepentogonInstaller::DownloadInstallRepentogonResult result = future.get();

		if (result == RepentogonInstaller::DOWNLOAD_INSTALL_REPENTOGON_ERR || 
			result == RepentogonInstaller::DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES) {
			_logWindow.LogError("Unable to force Repentogon update\n");
			return;
		}

		if (!_installation->CheckRepentogonInstallation()) {
			_logWindow.LogError("Installation of Repentogon post update is not valid\n");
			Launcher::DebugDumpBrokenRepentogonInstallation(_installation, _logWindow.Get());
		} else {
			_logWindow.Log("State of the Repentogon installation:\n");
			Launcher::DisplayRepentogonFilesVersion(_installation, 1, true, _logWindow.Get());
			UpdateRepentogonOptionsFromInstallation();
		}
	}

	std::string MainFrame::PromptIsaacInstallation() {
		wxString message("No Isaac installation found, please select Isaac executable to run");
		// wxDirDialog dialog(this, message, wxEmptyString, wxDD_DIR_MUST_EXIST, wxDefaultPosition, wxDefaultSize, "Select Isaac folder");
		wxFileDialog dialog(this, message, wxEmptyString, wxEmptyString, wxEmptyString, wxFD_FILE_MUST_EXIST, wxDefaultPosition,
			wxDefaultSize, "Select Isaac executable");
		dialog.ShowModal();
		return dialog.GetPath().ToStdString();
	}

	void MainFrame::AddLauncherConfigurationTextField(const char* intro,
		const char* buttonText, const char* emptyText, wxColour const& emptyColor, 
		wxBoxSizer* sizer, wxTextCtrl** result, Launcher::Windows windowId) {
		_configurationSizer->Add(new wxStaticText(_configurationBox, -1, intro), 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 20);
		wxButton* isaacSelectionButton = new wxButton(_configurationBox, windowId, buttonText);
		_configurationSizer->Add(isaacSelectionButton, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);
		wxTextCtrl* textCtrl = new wxTextCtrl(_configurationBox, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, -1), wxTE_READONLY | wxTE_RICH);
		textCtrl->SetBackgroundColour(*wxWHITE);

		wxTextAttr textAttr = textCtrl->GetDefaultStyle();
		textCtrl->SetDefaultStyle(wxTextAttr(emptyColor));
		textCtrl->AppendText(emptyText);
		textCtrl->SetDefaultStyle(textAttr);

		wxSizerFlags flags = wxSizerFlags().Expand().Proportion(1);
		_configurationSizer->Add(textCtrl, flags);

		if (result) {
			*result = textCtrl;
		}
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
			Updater::LauncherUpdateManager(&_logWindow).ForceSelfUpdate(true);
			break;

		case ADVANCED_EVENT_FORCE_LAUNCHER_UPDATE:
			Updater::LauncherUpdateManager(&_logWindow).ForceSelfUpdate(false);
			break;

		case ADVANCED_EVENT_FORCE_REPENTOGON_UNSTABLE_UPDATE:
			ForceRepentogonUpdate(true);
			break;

		case ADVANCED_EVENT_FORCE_REPENTOGON_UPDATE:
			ForceRepentogonUpdate(false);
			break;

		default:
			_logWindow.LogError("Unhandled result from ShowModal: %d", result);
			break;
		}
	}

	bool MainFrame::HandleIsaacExecutableSelection(std::string const& path) {
		return _installation->SetIsaacExecutable(path);
	}
}