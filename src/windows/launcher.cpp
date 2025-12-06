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

#include "chained_future/chained_future.h"
#include "comm/messages.h"
#include "curl/curl.h"
#include "launcher/app.h"
#include "launcher/windows/advanced_options.h"
#include "launcher/windows/checklogs_modal.h"
#include "launcher/installation.h"
#include "launcher/isaac.h"
#include "launcher/launcher_configuration.h"
#include "launcher/launcher_interface.h"
#include "launcher/log_helpers.h"
#include "launcher/launcher_self_update.h"
#include "launcher/windows/launcher.h"
#include "launcher/windows/repentogon_installer.h"
#include "launcher/windows/setup_wizard.h"
#include "launcher/windows/launch_countdown.h"
#include "launcher/repentogon_installer.h"
#include "launcher/windows/options_ini.h"
#include "launcher/version.h"
#include "shared/compat.h"
#include "shared/github.h"
#include "shared/filesystem.h"
#include "shared/launcher_update_checker.h"
#include "shared/logger.h"
#include "shared/monitor.h"
#include "shared/utils.h"

#include "zip.h"
#include "zipint.h"

#ifdef min
#undef min
#endif
#include <launcher/modmanager.h>
#include <launcher/modupdater.h>
#include <shtypes.h>
#include <ShObjIdl_core.h>
#include <ShlObj_core.h>

wxBEGIN_EVENT_TABLE(Launcher::LauncherMainWindow, wxFrame)
EVT_COMBOBOX(Launcher::WINDOW_COMBOBOX_LEVEL, Launcher::LauncherMainWindow::OnLevelSelect)
EVT_COMBOBOX(Launcher::WINDOW_COMBOBOX_LAUNCH_MODE, Launcher::LauncherMainWindow::OnLaunchModeSelect)
EVT_TEXT(Launcher::WINDOW_COMBOBOX_LAUNCH_MODE, Launcher::LauncherMainWindow::OnLaunchModeSelect)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_CONSOLE, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_UPDATES, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_HIDE_WINDOW, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_STEALTH_MODE, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_BUTTON(Launcher::WINDOW_BUTTON_LAUNCH_BUTTON, Launcher::LauncherMainWindow::Launch)
EVT_BUTTON(Launcher::WINDOW_BUTTON_SELECT_ISAAC, Launcher::LauncherMainWindow::OnIsaacSelectClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_CHECKLOGS_BUTTON, Launcher::LauncherMainWindow::OnCheckLogsClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_ADVANCED_OPTIONS, Launcher::LauncherMainWindow::OnAdvancedOptionsClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_ADVANCED_MOD_OPTIONS, Launcher::LauncherMainWindow::OnAdvancedModOptionsClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_MODMAN_BUTTON, Launcher::LauncherMainWindow::OnModManagerButtonPressed)
EVT_BUTTON(Launcher::WINDOW_BUTTON_CHANGEOPTIONS_BUTTON, Launcher::LauncherMainWindow::OnChangeOptions)
wxEND_EVENT_TABLE()

int chained_future_f(int a) {
	return a * 2;
}

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

	static constexpr const char* ComboBoxVanilla = "Vanilla";
	static constexpr const char* ComboBoxRepentogon = "REPENTOGON";

	static const std::regex LuaHeapSizeRegex("^([0-9]+[KMG]{0,1})?$");


	LauncherMainWindow::LauncherMainWindow(Installation* installation, LauncherConfiguration* configuration) :
			wxFrame(nullptr, wxID_ANY, "REPENTOGON Launcher"),
			_installation(installation), _configuration(configuration),
			_logWindow(new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, 125),
				wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH)) {
		_console = nullptr;

		// Main window sizer
		_verticalSizer = new wxBoxSizer(wxVERTICAL);

		// Log window
		wxTextCtrl* logWindow = _logWindow.Get();
		logWindow->SetBackgroundColour(*wxWHITE);
		const wxSizerFlags logFlags = wxSizerFlags().Expand().Proportion(5).Border(wxALL, 5);
		_verticalSizer->Add(logWindow, logFlags);

		// Shared sizer flags
		const wxSizerFlags sharedFlags = wxSizerFlags().Expand().Proportion(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, 5);

		// Launcher configuration
		_configurationBox = new wxStaticBox(this, -1, "Launcher configuration");
		_configurationSizer = new wxStaticBoxSizer(_configurationBox, wxVERTICAL);
		AddLauncherConfigurationOptions();
		_verticalSizer->Add(_configurationSizer, sharedFlags);

		// Game configuration
		_optionsBox = new wxStaticBox(this, -1, "Game configuration");
		_optionsSizer = new wxStaticBoxSizer(_optionsBox, wxVERTICAL);
		_optionsGridSizer = new wxFlexGridSizer(2, 2, 5);
		_optionsGridSizer->SetFlexibleDirection(wxBOTH);
		const wxSizerFlags optionsGridFlags = wxSizerFlags().Expand().Proportion(1).Border(wxALL, 0);
		AddRepentogonOptions(optionsGridFlags);
		AddModdingOptions(optionsGridFlags);
		AddLaunchOptions(optionsGridFlags);
		AddOptionsConfigOptions(optionsGridFlags);
		_optionsSizer->Add(_optionsGridSizer, 1, wxALIGN_CENTER_HORIZONTAL | wxALL, 2);
		_verticalSizer->Add(_optionsSizer, sharedFlags);

		// Launch button
		_launchButton = new wxButton(this, WINDOW_BUTTON_LAUNCH_BUTTON, "Launch game", wxDefaultPosition, wxSize(50, 50));
		_verticalSizer->Add(_launchButton, sharedFlags);

		SetSizerAndFit(_verticalSizer);

		SetBackgroundColour(wxColour(237, 237, 237));
		CenterOnScreen();
	}

	LauncherMainWindow::~LauncherMainWindow() {
		_configuration->Write();
	}

	void LauncherMainWindow::AddLauncherConfigurationOptions() {
		// Isaac exe row
		wxBoxSizer* isaacExeSizer = new wxBoxSizer(wxHORIZONTAL);

		// Isaac exe label
		wxStaticText* isaacExeText = new wxStaticText(_configurationBox, wxID_ANY, "Isaac executable", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
		isaacExeSizer->Add(isaacExeText, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);

		// Isaac exe path
		_isaacFileText = new wxTextCtrl(_configurationBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxTE_RICH);
		_isaacFileText->SetBackgroundColour(*wxWHITE);
		wxTextAttr textAttr = _isaacFileText->GetDefaultStyle();
		_isaacFileText->SetDefaultStyle(wxTextAttr(NoIsaacColor));
		_isaacFileText->AppendText(NoIsaacText);
		_isaacFileText->SetDefaultStyle(textAttr);
		isaacExeSizer->Add(_isaacFileText, 1, wxALIGN_CENTER_VERTICAL);

		// Choose exe button
		wxButton* isaacSelectionButton = new wxButton(_configurationBox, WINDOW_BUTTON_SELECT_ISAAC, "Choose exe");
		isaacExeSizer->Add(isaacSelectionButton, 0, wxLEFT, 2);

		_configurationSizer->Add(isaacExeSizer, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 5);
		// _configurationSizer->Add(new wxStaticLine(), 0, wxTOP | wxBOTTOM, 5);


		// REPENTOGON executable row
		wxBoxSizer* repentogonExeSizer = new wxBoxSizer(wxHORIZONTAL);

		// rgon exe label
		wxStaticText* repentogonExeText = new wxStaticText(_configurationBox, wxID_ANY, "REPENTOGON exe", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
		repentogonExeSizer->Add(repentogonExeText, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);

		// rgon exe path
		_repentogonFileText = new wxTextCtrl(_configurationBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
		repentogonExeSizer->Add(_repentogonFileText, wxSizerFlags().Proportion(1).Border(wxRIGHT, 2));

		_configurationSizer->Add(repentogonExeSizer, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 5);


		// Align the two path boxes
		const wxSize isaacExeTextSize = isaacExeText->GetTextExtent(isaacExeText->GetLabelText());
		const wxSize rgonExeTextSize = repentogonExeText->GetTextExtent(repentogonExeText->GetLabelText());
		const wxSize minSize(std::max(isaacExeTextSize.x, rgonExeTextSize.x), std::max(isaacExeTextSize.y, rgonExeTextSize.y));
		isaacExeText->SetMinSize(minSize);
		repentogonExeText->SetMinSize(minSize);


		// Additional Launcher options
		 _stealthMode = new wxCheckBox(_configurationBox, WINDOW_CHECKBOX_STEALTH_MODE, "Stealth Mode (Always ON in BigPicture mode and Steam Deck)");
		 _stealthMode->SetToolTip("When starting the launcher, skip the main window and automatically launch Isaac, then close the launcher afterwards.\n\nThe launcher will appear if an error occurs.");
		 _configurationSizer->Add(_stealthMode, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 5);

		/*
		_hideWindow = new wxCheckBox(_configurationBox, WINDOW_CHECKBOX_HIDE_WINDOW,
			"Hide launcher window while the game is running");
		_configurationSizer->Add(_hideWindow, 0, wxLEFT | wxRIGHT | wxTOP, 5);
		*/
	}

	void LauncherMainWindow::AddLaunchOptions(const wxSizerFlags& sizerFlags) {
		wxStaticBox* launchModeBox = new wxStaticBox(_optionsBox, -1, "Launch Options");
		wxStaticBoxSizer* launchModeBoxSizer = new wxStaticBoxSizer(launchModeBox, wxVERTICAL );

		wxSizer* box = new wxBoxSizer(wxHORIZONTAL);
		box->Add(new wxStaticText(launchModeBox, -1, "Launch mode: "), wxEXPAND | wxLEFT | wxRIGHT, 5);

		_launchMode = new wxComboBox(launchModeBox, WINDOW_COMBOBOX_LAUNCH_MODE, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
		_launchMode->Append(ComboBoxRepentogon);
		_launchMode->Append(ComboBoxVanilla);
		_launchMode->SetValue(ComboBoxRepentogon);

		box->Add(_launchMode, wxEXPAND | wxRIGHT, 10);

		launchModeBoxSizer->Add(box, 0, wxTOP | wxLEFT | wxRIGHT, 5);
		//launchModeBoxSizer->Add(new wxStaticLine(launchModeBox), 0, wxTOP | wxLEFT | wxRIGHT, 3);

		_optionsGridSizer->Add(launchModeBoxSizer, sizerFlags);
	}

	void LauncherMainWindow::AddModdingOptions(const wxSizerFlags& sizerFlags) {
		wxStaticBox* modOptionsBox = new wxStaticBox(_optionsBox, -1, "Modding Options");
		wxStaticBoxSizer* modOptionsSizer = new wxStaticBoxSizer(modOptionsBox, wxVERTICAL);

		const wxSizerFlags buttonSizerFlags = wxSizerFlags().Expand().Proportion(0).Border(wxALL, 3);

		wxButton* modman = new wxButton(modOptionsBox, WINDOW_BUTTON_MODMAN_BUTTON, "Open Mod Manager", wxDefaultPosition);
		modOptionsSizer->Add(modman, buttonSizerFlags);

		wxButton* checklogs = new wxButton(modOptionsBox, WINDOW_BUTTON_CHECKLOGS_BUTTON, "Check Game Logs", wxDefaultPosition);
		modOptionsSizer->Add(checklogs, buttonSizerFlags);

		_advancedModOptionsButton = new wxButton(modOptionsBox, WINDOW_BUTTON_ADVANCED_MOD_OPTIONS, "Advanced Mod Options", wxDefaultPosition);
		modOptionsSizer->Add(_advancedModOptionsButton, buttonSizerFlags);

		_optionsGridSizer->Add(modOptionsSizer, sizerFlags);
	}


	void LauncherMainWindow::AddOptionsConfigOptions(const wxSizerFlags& sizerFlags) {
		wxStaticBox* gameOptionsBox = new wxStaticBox(_optionsBox, -1, "Game Options");
		wxStaticBoxSizer* gameOptionsSizer = new wxStaticBoxSizer(gameOptionsBox, wxVERTICAL);

		wxButton* modman = new wxButton(gameOptionsBox, WINDOW_BUTTON_CHANGEOPTIONS_BUTTON, "Change Game Options", wxDefaultPosition);
		gameOptionsSizer->Add(modman, 0, wxEXPAND | wxALL, 3);

		_optionsGridSizer->Add(gameOptionsSizer, sizerFlags);
	}

	void LauncherMainWindow::AddRepentogonOptions(const wxSizerFlags& sizerFlags) {
		wxStaticBox* repentogonBox = new wxStaticBox(_optionsBox, -1, "REPENTOGON Options");
		wxStaticBoxSizer* repentogonBoxSizer = new wxStaticBoxSizer(repentogonBox, wxVERTICAL);

		_updates = new wxCheckBox(repentogonBox, WINDOW_CHECKBOX_REPENTOGON_UPDATES, "Automatically check for updates");
		_updates->SetValue(true);

		_console = new wxCheckBox(repentogonBox, WINDOW_CHECKBOX_REPENTOGON_CONSOLE, "Enable dev console window");
		_console->SetValue(false);

		_unstableRepentogon = new wxCheckBox(repentogonBox, WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES, "Upgrade to unstable versions");
		_unstableRepentogon->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
			if (_unstableRepentogon->IsChecked()) {
				_unstableRepentogon->SetValue(false);
				int res = wxMessageBox(
					"Unstable REPENTOGON versions are work-in-progress updates.\n"
					"Bugs and crashes will be more likely. This is not reccomended for most users.\n\n"
					"If you are a mod developer, DO NOT publish mods that depend on unstable versions!\n"
					"If you are not a mod developer... you probably shouldn't use these anyway.\n\n"
					"Enable unstable updates?",
					"WARNING: Unstable REPENTOGON Versions",
					wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
					this
				);
				_unstableRepentogon->SetValue(res == wxYES);
			}
			event.Skip();
		});
		_unstableRepentogon->SetValue(false);

		_advancedOptionsButton = new wxButton(repentogonBox, WINDOW_BUTTON_ADVANCED_OPTIONS, "Advanced options...");

		const wxSizerFlags checkboxFlags = wxSizerFlags().Left().Border(wxLEFT | wxBOTTOM | wxRIGHT, 5);
		repentogonBoxSizer->Add(_updates, checkboxFlags);
		repentogonBoxSizer->Add(_unstableRepentogon, checkboxFlags);
		repentogonBoxSizer->Add(_console, checkboxFlags);

		repentogonBoxSizer->Add(_advancedOptionsButton, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 0);

		_repentogonOptions = repentogonBox;
		_optionsGridSizer->Add(repentogonBoxSizer, sizerFlags);
	}

	void LauncherMainWindow::OnIsaacSelectClick(wxCommandEvent&) {
		SelectIsaacExecutablePath();
	}

	void LauncherMainWindow::OnModManagerButtonPressed(wxCommandEvent&) {
		ModManagerFrame* modWindow = new ModManagerFrame(this,_installation);
		modWindow->Show();
	}

	void LauncherMainWindow::OnChangeOptions(wxCommandEvent&) {
		GameOptions opts;
		opts.Load(fs::absolute(_configuration->GetConfigurationPath()).parent_path().string() + "/Binding of Isaac Repentance+/options.ini");

		OptionsDialog dlg(this, opts);
		dlg.ShowModal(); // modal; handles save internally
	}

	void LauncherMainWindow::OnFileSelected(std::string const& path, wxColor const& emptyColor, wxTextCtrl* ctrl,
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

	void LauncherMainWindow::OnLevelSelect(wxCommandEvent& event) {
		wxComboBox* box = dynamic_cast<wxComboBox*>(event.GetEventObject());
		wxString string = box->GetValue();
		std::cmatch match;
		const char* text = string.c_str().AsChar();
		if (std::regex_search(text, match, std::basic_regex("([0-9]+)\\.([0-9]+)"))) {
			int stage = std::stoi(match[1].str());
			int type = std::stoi(match[2].str());
			_configuration->SetStage(stage);
			_configuration->SetStageType(type);
		} else {
			_configuration->SetStage(0);
			_configuration->SetStageType(0);
		}
	}

	void LauncherMainWindow::OnLaunchModeSelect(wxCommandEvent& event) {
		wxComboBox* box = dynamic_cast<wxComboBox*>(event.GetEventObject());
		if (box->GetValue() == ComboBoxVanilla) {
			_configuration->SetIsaacLaunchMode(LAUNCH_MODE_VANILLA);
		} else {
			_configuration->SetIsaacLaunchMode(LAUNCH_MODE_REPENTOGON);
		}
		UpdateLaunchButtonEnabledState();
	}



	void LauncherMainWindow::OnOptionSelected(wxCommandEvent& event) {
		wxCheckBox* box = dynamic_cast<wxCheckBox*>(event.GetEventObject());
		switch (box->GetId()) {
		case WINDOW_CHECKBOX_REPENTOGON_CONSOLE:
			_configuration->SetRepentogonConsole(box->GetValue());
			break;

		case WINDOW_CHECKBOX_REPENTOGON_UPDATES:
			_configuration->SetAutomaticUpdates(box->GetValue());
			break;

		case WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES:
			if (_configuration->UnstableUpdates() != box->GetValue()) {
				_configuration->SetUnstableUpdates(box->GetValue());
				OnRepentogonUnstableStateSwitched();
			}
			break;

		case WINDOW_CHECKBOX_HIDE_WINDOW:
			_configuration->SetHideWindow(box->GetValue());
			break;

		case WINDOW_CHECKBOX_STEALTH_MODE:
			_configuration->SetStealthMode(box->GetValue());
			break;

		default:
			return;
		}
	}

	void LauncherMainWindow::OnRepentogonUnstableStateSwitched() {
		const bool unstableUpdatesEnabled = _unstableRepentogon->GetValue();

		const wxString message = wxString::Format("Unstable REPENTOGON updates are now %s.\n\n"
			"Would you like to download and install the latest %s version now?\n\n"
			"(Note: You can force a REPENTOGON update at any time from the \"Advanced Options\" menu.)",
			unstableUpdatesEnabled ? "ENABLED" : "DISABLED",
			unstableUpdatesEnabled ? "unstable" : "stable");
		const int result = wxMessageBox(message, "REPENTOGON Stability Option Changed", wxYES_NO, this);

		if (result == wxYES || result == wxOK) {
			ForceRepentogonUpdate(unstableUpdatesEnabled);
		}
	}

	void LauncherMainWindow::Launch(wxCommandEvent&) {
		LaunchIsaac();
	}

	LauncherMainWindow::IsaacLaunchability LauncherMainWindow::GetIsaacLaunchability() {
		const RepentogonInstallation& repentogonInstallation = _installation->GetRepentogonInstallation();
		const IsaacInstallation& isaacInstallation = _installation->GetIsaacInstallation();

		if (_configuration->IsaacLaunchMode() == LAUNCH_MODE_REPENTOGON) {
			if (!repentogonInstallation.IsValid()) {
				return ISAAC_LAUNCH_REPENTOGON_INVALID;
			}
			if (!isaacInstallation.GetRepentogonInstallation().IsCompatibleWithRepentogon()) {
				return ISAAC_LAUNCH_REPENTOGON_INCOMPATIBLE;
			}
			return ISAAC_LAUNCH_OK;
		} else if (_configuration->IsaacLaunchMode() == LAUNCH_MODE_VANILLA) {
			if (!isaacInstallation.GetMainInstallation().IsValid()) {
				return ISAAC_LAUNCH_VANILLA_INVALID;
			}
			return ISAAC_LAUNCH_OK;
		}

		return ISAAC_LAUNCH_UNKNOWN;
	}

	const char* LauncherMainWindow::GetIsaacLaunchabilityErrorMessage(const IsaacLaunchability launchability) {
		switch (launchability) {
		case ISAAC_LAUNCH_OK:
			return "No error - the game can be launched!";
		case ISAAC_LAUNCH_VANILLA_INVALID:
			return "The vanilla installation is broken!";
		case ISAAC_LAUNCH_REPENTOGON_INVALID:
			return "The REPENTOGON installation is broken!";
		case ISAAC_LAUNCH_REPENTOGON_INCOMPATIBLE:
			return "The Isaac executable is not compatible with REPENTOGON!";
		default:
			return "The game cannot be launched due to an unknown error!!!";
		}
	}

	bool LauncherMainWindow::LaunchIsaac(bool relaunch) {
		if (!relaunch && SteamAPI_Init() && SteamAPI_IsSteamRunning()) { //No point in running the updater if nonsteam....for now?, lol
			_logWindow.Log("Checking for mod updates on Steam's folder:");
			ModUpdateDialog dlg(nullptr, fs::path(_configuration->IsaacExecutablePath()).parent_path() / "Mods", &_logWindow);
			dlg.ShowModal();
		}

		const IsaacLaunchability launchability = GetIsaacLaunchability();
		if (launchability != ISAAC_LAUNCH_OK) {
			SetFocus();
			if (!IsShown()) {
				Show();
			}
			const wxString errMessage = wxString::Format("Failed to launch game: %s", GetIsaacLaunchabilityErrorMessage(launchability));
			Logger::Error("%s (%d)\n", errMessage.c_str().AsChar(), launchability);
			wxMessageDialog(this, errMessage, "REPENTOGON Launcher", wxOK | wxICON_ERROR).ShowModal();
			_logWindow.LogError(errMessage.c_str().AsChar());
			EnableInterface(true);
			return false;
		}

		_logWindow.Log("Launching the game with the following options:");
		_logWindow.Log("\tREPENTOGON:");
		bool launchingVanilla = _configuration->IsaacLaunchMode() == LAUNCH_MODE_VANILLA;
		if (launchingVanilla) {
			_logWindow.Log("\t\tREPENTOGON is disabled");
		} else {
			_logWindow.Log("\t\tREPENTOGON is enabled");
			_logWindow.Log("\t\tEnable REPENTOGON console window: %s", _configuration->RepentogonConsole() ? "yes" : "no");
		}
		_logWindow.Log("\tVanilla:");
		if (_configuration->Stage() > 0) {
			_logWindow.Log("\t\tStarting stage: %d.%d", _configuration->Stage(), _configuration->StageType());
		} else {
			_logWindow.Log("\t\tStarting stage: not selected");
		}
		_logWindow.Log("\t\tLua debug: %s", _configuration->LuaDebug() ? "yes" : "no");
		if (!_configuration->LuaHeapSize().empty()) {
			_logWindow.Log("\t\tLua heap size: %s", _configuration->LuaHeapSize().c_str());
		} else {
			_logWindow.Log("\t\tLua heap size: not set");
		}

		_configuration->Write();

		wxString path = _isaacFileText->GetValue();
		if (!launchingVanilla) {
			path = _repentogonFileText->GetValue();
		}

		std::string pathStr = path.ToStdString();
		_exePath = new char[pathStr.size() + 1];
		if (!_exePath) {
			_logWindow.LogError("Unable to allocate memory to launch the game\n");
			Logger::Error("Unable to allocate memory for executable path\n");
			return false;
		}

		strcpy(_exePath, pathStr.c_str());

		EnableInterface(false);

		if (_configuration->HideWindow() || _configuration->AutoLaunch()) {
			Show(false);
		}
		chained_futures::async(&::Launcher::Launch, &_logWindow, _exePath,
			launchingVanilla, _configuration
		).chain(std::bind_front(&LauncherMainWindow::OnIsaacCompleted, this));

		return true;
	}

	void LauncherMainWindow::OnIsaacCompleted(DWORD exitCode) {
		if (_exePath) {
			delete[] _exePath;
			_exePath = nullptr;
		}

		/* Need to fence here to prevent the compiler from reordering the call
		 * to delete and the reset of exePath to below this call.
		 *
		 * We want to enforce that the pointer has been freed before enabling
		 * the interface, otherwise we could have a memory leak if the user
		 * manages to launch the game before the delete completes.
		 */
		std::atomic_thread_fence(std::memory_order_relaxed);

		std::string desc;
		utils::ErrorCodeToString(exitCode, desc);

		if (exitCode && exitCode != LauncherInterface::LAUNCHER_EXIT_MODS_CHANGED &&
			exitCode != STATUS_CONTROL_C_EXIT) {
			if (exitCode != 0xFFFFFFFF) {
				_logWindow.LogWarn("Game exited with error code %#lx (%s)\n", exitCode, desc.c_str());
			}
			if (exitCode == 0xc0000135) { //missing dll, the lua5.3.3r will be missing in older versions of rgon with potentially fucked exes by the old launcher practices
				wxMessageBox(
					"This kind of error may indicate that you are missing an important RGON update! Make sure you are up to date!\n"
					"You can force an update in the Advanced Options screen.",
					"Broken Repentogon Instalation. Outdated?",
					wxOK | wxOK_DEFAULT | wxICON_ERROR,
					this
				);
			}
		} else {
			_logWindow.LogInfo("Game sucessfully exited\n");
		}

		EnableInterface(true);

		if (exitCode == LauncherInterface::LAUNCHER_EXIT_MODS_CHANGED) {
			RelaunchIsaac();
		} else if (_configuration->AutoLaunch()) {
			Destroy();
		} else if (!IsShown()) {
			Show();
		}
	}

	void LauncherMainWindow::RelaunchIsaac() {
		LaunchIsaac(true);
	}

	void LauncherMainWindow::EnableInterface(bool enable) {
		_configurationBox->Enable(enable);
		_advancedOptionsButton->Enable(enable);
		_optionsBox->Enable(enable);
		if (enable) {
			UpdateLaunchButtonEnabledState();
		} else {
			_launchButton->Disable();
		}
	}

	bool LauncherMainWindow::SelectIsaacExecutablePath() {
		LauncherWizard* wizard = new LauncherWizard(this, _installation, _configuration);
		wizard->AddPages(true);
		bool ok = wizard->Run();
		wizard->Destroy();

		RefreshGUIFromInstallation();

		return ok;
	}

	void LauncherMainWindow::RefreshGUIFromInstallation() {
		IsaacInstallation const& isaacInstallation = _installation->GetIsaacInstallation();
		RepentogonInstallation const& repentogonInstallation = _installation->GetRepentogonInstallation();

		InstallationData const& mainData = isaacInstallation.GetMainInstallation();
		if (mainData.IsValid()) {
			_isaacFileText->SetForegroundColour(*wxBLACK);
			_isaacFileText->SetValue(mainData.GetExePath());
		} else {
			_isaacFileText->SetForegroundColour(*wxRED);
			_isaacFileText->SetValue("No valid Isaac installation specified");
			_logWindow.LogError("No valid Isaac installation provided, "
				"cannot start until a correct value is provided !");
		}

		if (repentogonInstallation.IsValid()) {
			InstallationData const& repData = isaacInstallation.GetRepentogonInstallation();
			_repentogonFileText->SetForegroundColour(*wxBLACK);
			_repentogonFileText->SetValue(repData.GetExePath());
		} else {
			_repentogonFileText->SetForegroundColour(*wxRED);
			_repentogonFileText->SetValue("No valid REPENTOGON installation found");
			_logWindow.LogWarn("No valid REPENTOGON installation found");
		}

		UpdateRepentogonOptionsFromInstallation();
		_updates->SetValue(_configuration->AutomaticUpdates());
		_unstableRepentogon->SetValue(_configuration->UnstableUpdates());
	}

	bool LauncherMainWindow::SanityCheckLauncherUpdate() {
		return !Filesystem::Exists(Comm::UnpackedArchiveName);
	}

	void LauncherMainWindow::SanitizeLauncherUpdate() {
		if (!SanityCheckLauncherUpdate()) {
			_logWindow.LogWarn("Found launcher update file %s in the current folder."
				"This may indicate a broken update. Deleting it.\n", Comm::UnpackedArchiveName);
			Filesystem::RemoveFile(Comm::UnpackedArchiveName);
		}
	}

	void LauncherMainWindow::OneTimeIsaacPathInitialization() {
		std::string const& isaacPath = _installation->GetIsaacInstallation()
			.GetMainInstallation().GetExePath();
		if (!isaacPath.empty())
			_isaacFileText->SetValue(isaacPath);
		else {
			_logWindow.LogWarn("No Isaac installation found (this is probably a bug)\n");
			bool abortRequested = false;

			while (!abortRequested && !SelectIsaacExecutablePath()) {
				wxMessageDialog dialog(this, "Invalid Isaac executable given, do you want to retry ? (Answering \"No\" will abort)", "Invalid folder", wxYES_NO);
				int result = dialog.ShowModal();
				abortRequested = (result == wxID_NO);
			}

			if (abortRequested) {
				Logger::Fatal("User did not provide a valid Isaac installation folder, aborting\n");
			}
		}

		std::string const repentogonPath = _installation->GetIsaacInstallation()
			.GetRepentogonInstallation().GetExePath();
		if (repentogonPath.empty()) {
			_repentogonFileText->SetForegroundColour(*wxRED);
			_repentogonFileText->SetValue("No executable found");
		} else {
			_repentogonFileText->SetForegroundColour(*wxBLACK);
			_repentogonFileText->SetValue(repentogonPath);
		}
	}

	bool LauncherMainWindow::TryAutoLaunch() {
		if (!_configuration->AutoLaunch()) {
			return false;
		}
		if (_configuration->AutoLaunchNoCountdown()) {
			// Skip the countdown, just try to launch the game.
			return LaunchIsaac();
		}
		// Show the countdown modal that gives the user a chance to open the launcher.
		const int result = LaunchCountdownWindow(this).ShowModal();
		if (result == wxID_OK) {
			return LaunchIsaac();
		} else if (result != wxID_CANCEL) {
			_logWindow.LogWarn("Unexpected result from Launch Countdown modal: %d", result);
			Logger::Warn("Unexpected result from LaunchCountdownWindow ShowModal: %d", result);
		}
		return false;
	}

	void LauncherMainWindow::Init() {
/* #ifdef LAUNCHER_UNSTABLE
		wxMessageBox("You are running an unstable version of the REPENTOGON launcher.\n"
			"If you wish to run a stable version, use the \"Self-update (stable version)\" button",
			"Unstable launcher version", wxCENTER | wxICON_WARNING);
		LogWarn("Running an unstable version of the launcher");
#endif */
		_logWindow.Log("Welcome to the REPENTOGON Launcher (version %s)", Launcher::LAUNCHER_VERSION);
		std::string currentDir = Filesystem::GetCurrentDirectory_();
		_logWindow.Log("Current directory is: %s", currentDir.c_str());
		_logWindow.Log("Using configuration file %s\n", LauncherConfiguration::GetConfigurationPath());

		SanitizeLauncherUpdate();

		LPSTR cli = GetCommandLineA();
		_logWindow.Log("Command line: %s", cli);

		OneTimeIsaacPathInitialization();
		InitializeOptions();
		UpdateRepentogonOptionsFromInstallation();

		// Attempt to automatically launch isaac without showing the main window, depending on the cli/configs.
		// Reveal the main window no auto-launch is performed (either due to no attempt, or a failed attempt).
		if (!TryAutoLaunch()) {
			SetFocus();
			EnableInterface(true);
			if (!IsShown()) {
				Show();
			}
		}
	}

	bool LauncherMainWindow::PromptBoolean(wxString const& message, wxString const& shortMessage) {
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

		box->Insert("--", pos++);
		box->SetSelection(0);

		for (const IsaacInterface::Stage& stage : IsaacInterface::stages) {
			box->Insert(wxString().Format("%s (%d.%d)", stage.name, stage.level, stage.type), pos++, (void*)nullptr);
		}

		return box;
	}

	void LauncherMainWindow::InitializeOptions() {
		SanitizeConfiguration();
		InitializeGUIFromOptions();
	}

	void LauncherMainWindow::SanitizeConfiguration() {
		namespace c = Configuration;
		LaunchMode mode = _configuration->IsaacLaunchMode();
		if (mode != LAUNCH_MODE_REPENTOGON && mode != LAUNCH_MODE_VANILLA) {
			_logWindow.LogWarn("Invalid value %d for %s field in configuration file. Overriding with default", mode, c::Keys::launchMode.c_str());
			_configuration->SetIsaacLaunchMode(LAUNCH_MODE_REPENTOGON);
		}

		std::string luaHeapSize = _configuration->LuaHeapSize();
		if (!luaHeapSize.empty()) {
			(void)std::remove_if(luaHeapSize.begin(), luaHeapSize.end(), [](char c) -> bool { return std::isspace(c); });
			std::cmatch match;
			if (!std::regex_match(luaHeapSize.c_str(), match, LuaHeapSizeRegex)) {
				_logWindow.LogWarn("Invalid value %s for %s field in configuration file. Ignoring\n",
					luaHeapSize.c_str(), c::Keys::luaHeapSize.c_str());
				_configuration->SetLuaHeapSize(c::Defaults::luaHeapSize);
			} else {
				_configuration->SetLuaHeapSize(luaHeapSize);
			}
		}

		// Validate LevelStage
		int levelStage = _configuration->Stage();
		if (levelStage < IsaacInterface::STAGE_NULL || levelStage > IsaacInterface::STAGE8) {
			_logWindow.LogWarn("Invalid value %d for %s field in configuration file. Overriding with default",
				levelStage, c::Keys::levelStage.c_str());
			_configuration->SetStage(c::Defaults::levelStage);
			_configuration->SetStageType(c::Defaults::stageType);
		}

		// Validate StageType
		int stageType = _configuration->StageType();
		if (stageType < IsaacInterface::STAGETYPE_ORIGINAL || stageType > IsaacInterface::STAGETYPE_REPENTANCE_B) {
			_logWindow.LogWarn("Invalid value %d for %s field in configuration file. Overriding with default",
				stageType, c::Keys::stageType.c_str());
			_configuration->SetStageType(c::Defaults::stageType);
		}

		// Validate the combination of LevelStage+StageType
		if (levelStage > IsaacInterface::STAGE_NULL && !IsaacInterface::IsValidStage(levelStage, stageType)) {
			_logWindow.LogWarn("Invalid value %d for %s field associated with value %d "
				"for %s field in configuration file. Overriding with default", stageType, c::Keys::stageType.c_str(),
				levelStage, c::Keys::levelStage.c_str());
			_configuration->SetStageType(c::Defaults::stageType);
		}
	}

	void LauncherMainWindow::InitializeGUIFromOptions() {
		_configurationBox->GetWindowChild(WINDOW_BUTTON_SELECT_ISAAC)->Enable(!_configuration->IsaacExecutablePathHasOverride());

		_console->SetValue(_configuration->RepentogonConsole());
		_console->Enable(!_configuration->RepentogonConsoleHasOverride());

		_updates->SetValue(_configuration->AutomaticUpdates());
		_updates->Enable(!_configuration->AutomaticUpdatesHasOverride());

		_unstableRepentogon->SetValue(_configuration->UnstableUpdates());
		_unstableRepentogon->Enable(!_configuration->UnstableUpdatesHasOverride());

		 _stealthMode->SetValue(_configuration->StealthMode());
		 _stealthMode->Enable(!_configuration->StealthModeHasOverride());

		//_hideWindow->SetValue(_configuration->HideWindow());
		//_hideWindow->Enable(!_configuration->HideWindowHasOverride());

		//InitializeLevelSelectFromOptions();

		if (_configuration->IsaacLaunchMode() == LAUNCH_MODE_REPENTOGON) {
			_launchMode->SetValue(ComboBoxRepentogon);
		} else {
			_launchMode->SetValue(ComboBoxVanilla);
		}
		_launchMode->Enable(!_configuration->IsaacLaunchModeHasOverride());
	}

	/*
	void LauncherMainWindow::InitializeLevelSelectFromOptions() {
		int levelStage = _configuration->Stage();
		int stageType = _configuration->StageType();

		const char* stageName = IsaacInterface::GetStageName(levelStage, stageType);
		if (stageName) {
			_levelSelect->SetValue(wxString().Format("%s (%d.%d)", stageName, levelStage, stageType));
		} else {
			_levelSelect->SetSelection(0);
		}
		_levelSelect->Enable(!_configuration->StageHasOverride());
	}
	*/

	void LauncherMainWindow::UpdateLaunchButtonEnabledState() {
		if (_launchButton) {
			_launchButton->Enable(GetIsaacLaunchability() == ISAAC_LAUNCH_OK);
		}
	}

	void LauncherMainWindow::UpdateRepentogonOptionsFromInstallation() {
		const IsaacLaunchability launchability = GetIsaacLaunchability();
		if (launchability != ISAAC_LAUNCH_OK && _configuration->IsaacLaunchMode() == LAUNCH_MODE_REPENTOGON) {
			_logWindow.LogWarn("Disabling REPENTOGON start option: %s", GetIsaacLaunchabilityErrorMessage(launchability));
			if ((!attemptedpatchfix) && (((launchability == ISAAC_LAUNCH_REPENTOGON_INCOMPATIBLE) || (launchability == ISAAC_LAUNCH_REPENTOGON_INVALID)) && (_installation->GetIsaacInstallation().GetMainInstallation().IsCompatibleWithRepentogon()))) { // if the rgon exe is fucked but it seems fixable....try to fix it?
				attemptedpatchfix = true;
				_logWindow.LogWarn("Attempting to fix by forcing a rgon update/copy/patch!");
				if (std::filesystem::exists(_installation->GetIsaacInstallation().GetMainInstallation().GetFolderPath() + "\\Repentogon\\Isaac-ng.exe.bak")) {
					std::filesystem::remove(_installation->GetIsaacInstallation().GetMainInstallation().GetFolderPath() + "\\Repentogon\\Isaac-ng.exe.bak");
				}
				if (std::filesystem::exists(_installation->GetIsaacInstallation().GetMainInstallation().GetFolderPath() + "\\Repentogon\\Isaac-ng.exe")) {
					std::filesystem::rename(_installation->GetIsaacInstallation().GetMainInstallation().GetFolderPath() + "\\Repentogon\\Isaac-ng.exe", _installation->GetIsaacInstallation().GetMainInstallation().GetFolderPath() + "\\Repentogon\\Isaac-ng.exe.bak");
					std::filesystem::remove(_installation->GetIsaacInstallation().GetMainInstallation().GetFolderPath() + "\\Repentogon\\Isaac-ng.exe");
				}
				ForceRepentogonUpdate(_unstableRepentogon->GetValue());
			}
		}
		UpdateLaunchButtonEnabledState();
	}

	void LauncherMainWindow::ForceRepentogonUpdate(bool allowPreReleases) {
		_logWindow.Log("Forcibly updating REPENTOGON to the latest version");

		EnableInterface(false);
		std::shared_ptr<RepentogonInstallerFrame> updater(
			new RepentogonInstallerFrame(this, true, _installation, true, allowPreReleases)
		);
		updater->Initialize();
		updater->Show();
		chained_future<void> future = chained_futures::async(&RepentogonInstallerFrame::InstallRepentogon,
			updater.get()).chain(std::bind_front(&LauncherMainWindow::OnForceUpdateCompleted, this, updater));
	}

	void LauncherMainWindow::OnForceUpdateCompleted(std::shared_ptr<RepentogonInstallerFrame> frame) {
		Launcher::RepentogonInstaller::DownloadInstallRepentogonResult result =
			frame->GetDownloadInstallResult();

		frame->Destroy();
		RefreshGUIFromInstallation();
		SetFocus();
		EnableInterface(true);

		if (result == RepentogonInstaller::DOWNLOAD_INSTALL_REPENTOGON_ERR ||
			result == RepentogonInstaller::DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES) {
			_logWindow.LogError("Unable to force REPENTOGON update\n");
			return;
		}

		if (!_installation->CheckRepentogonInstallation()) {
			_logWindow.LogError("Installation of REPENTOGON post update is not valid\n");
			Launcher::DebugDumpBrokenRepentogonInstallation(_installation, _logWindow.Get());
		} else {
			_logWindow.Log("State of the REPENTOGON installation:\n");
			Launcher::DisplayRepentogonFilesVersion(_installation, 1, true, _logWindow.Get());
		}
	}

	void LauncherMainWindow::ForceLauncherUpdate(bool allowUnstable) {
		_logWindow.Log("Performing self-update (forcibly triggered)...");
		Launcher::HandleSelfUpdate(this, allowUnstable, true);
	}

	std::string LauncherMainWindow::PromptIsaacInstallation() {
		wxString message("No Isaac installation found, please select Isaac executable to run");
		// wxDirDialog dialog(this, message, wxEmptyString, wxDD_DIR_MUST_EXIST, wxDefaultPosition, wxDefaultSize, "Select Isaac folder");
		wxFileDialog dialog(this, message, wxEmptyString, wxEmptyString, wxEmptyString, wxFD_FILE_MUST_EXIST, wxDefaultPosition,
			wxDefaultSize, "Select Isaac executable");
		dialog.ShowModal();
		return dialog.GetPath().ToStdString();
	}

	bool LauncherMainWindow::PromptLegacyUninstall() {
		std::ostringstream s;
		s << "An unsupported build of REPENTOGON is currently installed and must be removed in order to upgrade to the latest Repentgon version.\n"
			"Proceed with its uninstallation?";
		wxMessageDialog modal(this, s.str(), "Uninstall legacy REPENTOGON installation?", wxYES_NO);
		int result = modal.ShowModal();
		return result == wxID_YES || result == wxID_OK;
	}

	void LauncherMainWindow::OnAdvancedOptionsClick(wxCommandEvent&) {
		AdvancedOptionsWindow window(this);
		int result = window.ShowModal();

		switch (result) {
		// User exited the modal without doing anything
		case wxID_CANCEL:
		case ADVANCED_EVENT_NONE:
			break;

		case ADVANCED_EVENT_FORCE_LAUNCHER_UPDATE:
			ForceLauncherUpdate(false);
			break;
		case ADVANCED_EVENT_REINSTALL:
			_logWindow.Log("Attempting Repair...");
			if (fs::remove_all(_installation->GetIsaacInstallation().GetMainInstallation().GetFolderPath() + "Repentogon")) {
				ForceRepentogonUpdate(GetRepentogonUnstableUpdatesState());
			}
			else {
				_logWindow.Log("Repair Failed!, Could not remove the Repentogon folder!");
			}
			break;
		case ADVANCED_EVENT_FORCE_REPENTOGON_UPDATE:
			ForceRepentogonUpdate(GetRepentogonUnstableUpdatesState());
			break;

		default:
			_logWindow.LogError("Unhandled result from ShowModal: %d", result);
			break;
		}
	}

	void LauncherMainWindow::OnAdvancedModOptionsClick(wxCommandEvent&) {
		AdvancedModOptionsWindow window(this);
		window.ShowModal();
		window.luaheapSize->Enable(!_configuration->LuaHeapSizeHasOverride());
		window.luaDebug->Enable(!_configuration->LuaDebugHasOverride());
	}

	void ShowFileInExplorer(const std::string& filePath) { //played around a few alternatives I found online, most suggested executing shell scripts....but those are slow as sin and if the user does anything before they execute, the fileexplorer window may not be on focus anymore....so its not ideal
		std::wstring stemp = std::wstring(filePath.begin(), filePath.end());
		PIDLIST_ABSOLUTE pidl = nullptr;
		HRESULT hr = SHParseDisplayName(stemp.c_str(), nullptr, &pidl, 0, nullptr);
		if (SUCCEEDED(hr)) {
			SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
			CoTaskMemFree(pidl);
		}
	}

	void LauncherMainWindow::OnCheckLogsClick(wxCommandEvent&) {
		CheckLogsWindow window(this);
		int result = window.ShowModal();

		switch (result) {
			// User exited the modal without doing anything
		case wxID_CANCEL:
		case CHECKLOGS_EVENT_NONE:
			break;

		case CHECKLOGS_EVENT_LAUNCHERLOG:
			wxLaunchDefaultBrowser("file:///" + fs::absolute("launcher.log").string());
			break;

		case CHECKLOGS_EVENT_RGONLOG:
			wxLaunchDefaultBrowser("file:///" + fs::absolute(_installation->GetIsaacInstallation()
				.GetRepentogonInstallation().GetExePath()).parent_path().string() + "/repentogon.log");
			break;

		case CHECKLOGS_EVENT_GAMELOG:
			wxLaunchDefaultBrowser("file:///" + fs::absolute(_configuration->GetConfigurationPath()).parent_path().string() + "/Binding of Isaac Repentance+/log.txt");
			break;

		case CHECKLOGS_EVENT_LAUNCHERLOG_LOCATE:
			ShowFileInExplorer("file:///" + fs::absolute("launcher.log").string());
			break;

		case CHECKLOGS_EVENT_RGONLOG_LOCATE:
			ShowFileInExplorer("file:///" + fs::absolute(_installation->GetIsaacInstallation()
				.GetRepentogonInstallation().GetExePath()).parent_path().string() + "/repentogon.log");
			break;

		case CHECKLOGS_EVENT_GAMELOG_LOCATE:
			ShowFileInExplorer("file:///" + fs::absolute(_configuration->GetConfigurationPath()).parent_path().string() + "/Binding of Isaac Repentance+/log.txt");
			break;


		default:
			_logWindow.LogError("Unhandled result from ShowModal: %d", result);
			break;
		}
	}
}