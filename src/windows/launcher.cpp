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
#include "launcher/installation.h"
#include "launcher/isaac.h"
#include "launcher/launcher_configuration.h"
#include "launcher/launcher_interface.h"
#include "launcher/log_helpers.h"
#include "launcher/launcher_self_update.h"
#include "launcher/windows/launcher.h"
#include "launcher/windows/repentogon_installer.h"
#include "launcher/windows/setup_wizard.h"
#include "launcher/repentogon_installer.h"
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

wxBEGIN_EVENT_TABLE(Launcher::LauncherMainWindow, wxFrame)
EVT_COMBOBOX(Launcher::WINDOW_COMBOBOX_LEVEL, Launcher::LauncherMainWindow::OnLevelSelect)
EVT_COMBOBOX(Launcher::WINDOW_COMBOBOX_LAUNCH_MODE, Launcher::LauncherMainWindow::OnLaunchModeSelect)
EVT_TEXT(Launcher::WINDOW_COMBOBOX_LAUNCH_MODE, Launcher::LauncherMainWindow::OnLaunchModeSelect)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_CONSOLE, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_UPDATES, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_VANILLA_LUADEBUG, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_HIDE_WINDOW, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_TEXT(Launcher::WINDOW_TEXT_VANILLA_LUAHEAPSIZE, Launcher::LauncherMainWindow::OnLuaHeapSizeCharacterWritten)
EVT_BUTTON(Launcher::WINDOW_BUTTON_LAUNCH_BUTTON, Launcher::LauncherMainWindow::Launch)
EVT_BUTTON(Launcher::WINDOW_BUTTON_SELECT_ISAAC, Launcher::LauncherMainWindow::OnIsaacSelectClick)
// EVT_BUTTON(Launcher::WINDOW_BUTTON_SELECT_REPENTOGON_FOLDER, Launcher::LauncherMainWindow::OnSelectRepentogonFolderClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_ADVANCED_OPTIONS, Launcher::LauncherMainWindow::OnAdvancedOptionsClick)
EVT_BUTTON(Launcher::WINDOW_BUTTON_MODMAN_BUTTON, Launcher::LauncherMainWindow::OnModManagerButtonPressed)
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
	static constexpr const char* ComboBoxRepentogon = "Repentogon";

	static const std::regex LuaHeapSizeRegex("^([0-9]+[KMG]{0,1})?$");

	LauncherMainWindow::LauncherMainWindow(Installation* installation, LauncherConfiguration* configuration) :
		wxFrame(nullptr, wxID_ANY, "REPENTOGON Launcher"),
		_installation(installation), _configuration(configuration),
		_logWindow(new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, -1),
			wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH)) {
		// _optionsGrid = new wxGridBagSizer(0, 20);
		_console = nullptr;
		_luaHeapSize = nullptr;

		wxStaticBox* optionsBox = new wxStaticBox(this, -1, "Game configuration");
		wxStaticBoxSizer* optionsSizer = new wxStaticBoxSizer(optionsBox, wxHORIZONTAL);

		_verticalSizer = new wxBoxSizer(wxVERTICAL);

		wxTextCtrl* logWindow = _logWindow.Get();
		logWindow->SetBackgroundColour(*wxWHITE);

		wxStaticBox* configurationBox = new wxStaticBox(this, -1, "Launcher configuration");
		wxStaticBoxSizer* configurationSizer = new wxStaticBoxSizer(configurationBox, wxVERTICAL);

		wxSizerFlags logFlags = wxSizerFlags().Expand().Proportion(5).Border(wxLEFT | wxRIGHT | wxTOP);
		_verticalSizer->Add(logWindow, logFlags);
		logFlags.Proportion(0);
		_verticalSizer->Add(configurationSizer, logFlags);
		_verticalSizer->Add(optionsSizer, logFlags);

		_configurationBox = configurationBox;
		_configurationSizer = configurationSizer;
		_optionsSizer = optionsSizer;
		_optionsBox = optionsBox;

		AddLauncherConfigurationOptions();
		AddRepentogonOptions();
		AddVanillaOptions();

		_launchnmoddingSizer = new wxBoxSizer(wxVERTICAL);

		AddLaunchOptions();
		AddModdingOptions();

		_optionsSizer->Add(_launchnmoddingSizer, 0, wxALL, 0);

		SetSizerAndFit(_verticalSizer);

		SetBackgroundColour(wxColour(237, 237, 237));

	}

	LauncherMainWindow::~LauncherMainWindow() {
		_configuration->Write();
	}

	void LauncherMainWindow::AddLauncherConfigurationOptions() {
		wxBoxSizer* isaacSelectionSizer = new wxBoxSizer(wxHORIZONTAL);
		AddLauncherConfigurationTextField("Isaac executable",
			"Choose exe", NoIsaacText,
			NoIsaacColor, isaacSelectionSizer, &_isaacFileText, WINDOW_BUTTON_SELECT_ISAAC);

		_configurationSizer->Add(isaacSelectionSizer, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 5);
		// _configurationSizer->Add(new wxStaticLine(), 0, wxTOP | wxBOTTOM, 5);

		wxBoxSizer* repentogonExeSizer = new wxBoxSizer(wxHORIZONTAL);
		wxStaticText* repentogonExeText = new wxStaticText(_configurationBox, wxID_ANY, "Repentogon executable",wxDefaultPosition);
		_repentogonFileText = new wxTextCtrl(_configurationBox, wxID_ANY, "", wxDefaultPosition,
			wxDefaultSize, wxTE_READONLY);
		repentogonExeSizer->Add(repentogonExeText, 0, wxRIGHT, 5);
		repentogonExeSizer->Add(_repentogonFileText, wxSizerFlags().Proportion(1).Expand().Border( wxRIGHT));
		_configurationSizer->Add(repentogonExeSizer, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 5);

		_hideWindow = new wxCheckBox(_configurationBox, WINDOW_CHECKBOX_HIDE_WINDOW,
			"Hide launcher window while the game is running");
		_configurationSizer->Add(_hideWindow, 0, wxLEFT | wxRIGHT | wxTOP, 5);
	}

	void LauncherMainWindow::AddLaunchOptions() {
		wxStaticBox* launchModeBox = new wxStaticBox(_optionsBox, -1, "Launch Options");
		wxStaticBoxSizer* launchModeBoxSizer = new wxStaticBoxSizer(launchModeBox, wxVERTICAL);

		wxSizer* box = new wxBoxSizer(wxHORIZONTAL);
		box->Add(new wxStaticText(launchModeBox, -1, "Launch mode: "));

		_launchButton = new wxButton(this, WINDOW_BUTTON_LAUNCH_BUTTON, "Launch game", wxDefaultPosition, wxSize(50, 50));
		//launchModeBoxSizer->Add(_launchButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

		_launchMode = new wxComboBox(launchModeBox, WINDOW_COMBOBOX_LAUNCH_MODE);
		_launchMode->Append(ComboBoxRepentogon);
		_launchMode->Append(ComboBoxVanilla);
		_launchMode->SetValue(ComboBoxRepentogon);

		box->Add(_launchMode);

		launchModeBoxSizer->Add(box, 0, wxTOP | wxLEFT | wxRIGHT, 0);

		launchModeBoxSizer->Add(new wxStaticLine(launchModeBox), 0, wxBOTTOM, 0);

		_launchnmoddingSizer->Add(launchModeBoxSizer, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 0);
		_verticalSizer->Add(_launchButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
		_launchnmoddingSizer->AddSpacer(10);
	}

	void LauncherMainWindow::AddModdingOptions() {
		wxStaticBox* repentogonBox = new wxStaticBox(_optionsBox, -1, "Modding Options");
		wxStaticBoxSizer* repentogonBoxSizer = new wxStaticBoxSizer(repentogonBox, wxVERTICAL);

		wxButton* modman = new wxButton(repentogonBox, WINDOW_BUTTON_MODMAN_BUTTON, "Open Mod Manager", wxDefaultPosition);
		repentogonBoxSizer->Add(modman, 1, wxEXPAND | wxALL, 5);
		_launchnmoddingSizer->Add(repentogonBoxSizer, 1, wxEXPAND | wxALL, 0);
	}

	void LauncherMainWindow::AddRepentogonOptions() {
		wxStaticBox* repentogonBox = new wxStaticBox(_optionsBox, -1, "REPENTOGON Options");
		wxStaticBoxSizer* repentogonBoxSizer = new wxStaticBoxSizer(repentogonBox, wxVERTICAL);

		wxCheckBox* updates = new wxCheckBox(repentogonBox, WINDOW_CHECKBOX_REPENTOGON_UPDATES, "Automatically check for updates");
		updates->SetValue(true);

		wxCheckBox* console = new wxCheckBox(repentogonBox, WINDOW_CHECKBOX_REPENTOGON_CONSOLE, "Enable console window");
		console->SetValue(false);

		wxCheckBox* unstable = new wxCheckBox(repentogonBox, WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES, "Upgrade to unstable versions");
		unstable->SetValue(false);

		_advancedOptionsButton = new wxButton(repentogonBox, WINDOW_BUTTON_ADVANCED_OPTIONS, "Advanced options...");
		wxSizerFlags advancedOptionsFlags = wxSizerFlags().Right();


		_updates = updates;
		_console = console;
		_unstableRepentogon = unstable;

		repentogonBoxSizer->Add(console, 0, wxTOP | wxLEFT | wxRIGHT, 5);
		repentogonBoxSizer->Add(updates, 0, wxLEFT | wxRIGHT, 5);
		repentogonBoxSizer->Add(unstable, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
		repentogonBoxSizer->Add(_advancedOptionsButton, 0, wxCENTER | wxBOTTOM, 5);

		_repentogonOptions = repentogonBox;
		_optionsSizer->Add(repentogonBoxSizer, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 10);
	}

	void LauncherMainWindow::AddVanillaOptions() {
		wxStaticBox* vanillaBox = new wxStaticBox(_optionsBox, -1, "Vanilla Options");
		wxStaticBoxSizer* vanillaBoxSizer = new wxStaticBoxSizer(vanillaBox, wxVERTICAL);

		wxSizer* levelSelectSizer = new wxBoxSizer(wxHORIZONTAL);
		levelSelectSizer->Add(new wxStaticText(vanillaBox, -1, "Starting stage: "));
		_levelSelect = CreateLevelsComboBox(vanillaBox);
		levelSelectSizer->Add(_levelSelect);

		vanillaBoxSizer->Add(levelSelectSizer, 0, wxTOP | wxLEFT | wxRIGHT, 5);
		_luaDebug = new wxCheckBox(vanillaBox, WINDOW_CHECKBOX_VANILLA_LUADEBUG, "Enable luadebug (unsafe)");
		_luaDebug->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
			if (_luaDebug->IsChecked()) {
				_luaDebug->SetValue(false);
				int res = wxMessageBox(
					"By enabling this, you give all your auto-updating workshop mods FULL UNRESTRICTED ACCESS TO YOUR COMPUTER!!, they can delete files, encrypt your harddrive, mine bitcoins, send out your login tokens, you name it...\nAre you sure you want to enable it?",
					"Enable luadebug",
					wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
					this
				);
				_luaDebug->SetValue(res == wxYES);
			}
			event.Skip();
		});

		vanillaBoxSizer->Add(_luaDebug, 0, wxLEFT | wxRIGHT, 5);
		vanillaBoxSizer->Add(new wxStaticLine(vanillaBox), 0, wxTOP | wxBOTTOM, 5);

		wxSizer* heapSizeBox = new wxBoxSizer(wxHORIZONTAL);
		wxTextCtrl* heapSizeCtrl = new wxTextCtrl(vanillaBox, WINDOW_TEXT_VANILLA_LUAHEAPSIZE, "1024M");
		_luaHeapSize = heapSizeCtrl;
		wxStaticText* heapSizeText = new wxStaticText(vanillaBox, -1, "Lua heap size: ");
		heapSizeBox->Add(heapSizeText);
		heapSizeBox->Add(heapSizeCtrl);
		vanillaBoxSizer->Add(heapSizeBox, 0, wxLEFT | wxRIGHT, 5);
		vanillaBoxSizer->Add(new wxStaticLine(vanillaBox), 0, wxBOTTOM, 5);

		_optionsSizer->Add(vanillaBoxSizer, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 10);
	}

	void LauncherMainWindow::OnIsaacSelectClick(wxCommandEvent&) {
		SelectIsaacExecutablePath();
	}

	/* void LauncherMainWindow::OnSelectRepentogonFolderClick(wxCommandEvent& event) {
		wxDirDialog dialog(this, "Please select the folder in which to install Repentogon", wxEmptyString,
			0, wxDefaultPosition, wxDefaultSize, "Select Repentogon installation folder");
		dialog.ShowModal();

		std::string path = dialog.GetPath().ToStdString();
		OnFileSelected(path, NoRepentogonInstallationFolderColor, _repentogonInstallFolderText, NoRepentogonInstallationFolderText);
	} */

	void LauncherMainWindow::OnModManagerButtonPressed(wxCommandEvent&) {
		ModManagerFrame* modWindow = new ModManagerFrame(this,_installation);
		modWindow->Show();
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
			_configuration->Stage(stage);
			_configuration->StageType(type);
		} else {
			_configuration->Stage(0);
			_configuration->StageType(0);
		}
	}

	void LauncherMainWindow::OnLaunchModeSelect(wxCommandEvent& event) {
		wxComboBox* box = dynamic_cast<wxComboBox*>(event.GetEventObject());
		if (box->GetValue() == ComboBoxVanilla) {
			_configuration->IsaacLaunchMode(LAUNCH_MODE_VANILLA);
		} else {
			_configuration->IsaacLaunchMode(LAUNCH_MODE_REPENTOGON);
		}
		UpdateLaunchButtonEnabledState();
	}

	void LauncherMainWindow::OnLuaHeapSizeCharacterWritten(wxCommandEvent& event) {
		wxTextCtrl* ctrl = dynamic_cast<wxTextCtrl*>(event.GetEventObject());

		std::string luaHeapSize = ctrl->GetValue().c_str().AsChar();
		std::transform(luaHeapSize.begin(), luaHeapSize.end(), luaHeapSize.begin(), toupper);

		std::cmatch match;
		if (std::regex_match(luaHeapSize.c_str(), match, LuaHeapSizeRegex)) {
			_configuration->LuaHeapSize(luaHeapSize);
		} else if (luaHeapSize == "K" || luaHeapSize == "M" || luaHeapSize == "G") {
			_configuration->LuaHeapSize("");
		} else {
			wxBell();
			const long ip = ctrl->GetInsertionPoint() - 1;
			ctrl->SetValue(_configuration->LuaHeapSize());
			ctrl->SetInsertionPoint(ip > 0 ? ip : 0);
		}
	}

	void LauncherMainWindow::OnOptionSelected(wxCommandEvent& event) {
		wxCheckBox* box = dynamic_cast<wxCheckBox*>(event.GetEventObject());
		switch (box->GetId()) {
		case WINDOW_CHECKBOX_REPENTOGON_CONSOLE:
			_configuration->RepentogonConsole(box->GetValue());
			break;

		case WINDOW_CHECKBOX_REPENTOGON_UPDATES:
			_configuration->AutomaticUpdates(box->GetValue());
			break;

		case WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES:
			_configuration->UnstableUpdates(box->GetValue());
			if (box->GetValue() != _initialUnstableUpdates) {
				OnRepentogonUnstableStateSwitched();
			}
			break;

		case WINDOW_CHECKBOX_VANILLA_LUADEBUG:
			_configuration->LuaDebug(box->GetValue());
			break;

		case WINDOW_CHECKBOX_HIDE_WINDOW:
			_configuration->HideWindow(box->GetValue());
			break;

		default:
			return;
		}
	}

	void LauncherMainWindow::OnRepentogonUnstableStateSwitched() {
		if (_canPromptOnUnstableSwitch) {
			int result = wxMessageBox("You have switched the Repentogon stability option. "
				"Do you want to immediately install the corresponding release ?\n"
				"\n"
				"If you answer \"No\", the launcher will not prompt you again on repeat switches. "
				"You can use the \"Advanced Options\" to forcibly trigger the installation later.",
				"Repentogon stability option changed",
				wxYES_NO, this);

			if (result == wxYES || result == wxOK) {
				ForceRepentogonUpdate(_unstableRepentogon->GetValue());
			} else {
				_canPromptOnUnstableSwitch = false;
			}
		} else {
			_logWindow.LogWarn("You previously chose to not install Repentogon after "
				"switching the unstable Repentogon option. If you want to force an "
				"update, use the \"Advanced Options\" button below.\n");
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

	bool LauncherMainWindow::LaunchIsaac() {
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

		if (SteamAPI_Init() && SteamAPI_IsSteamRunning()) { //No point in running the updater if nonsteam....for now?, lol
			_logWindow.Log("Checking for mod updates on Steam's folder:");
			ModUpdateDialog dlg(nullptr, fs::path(_configuration->IsaacExecutablePath()).parent_path() / "Mods", &_logWindow);
			dlg.ShowModal();
		}

		_logWindow.Log("Launching the game with the following options:");
		_logWindow.Log("\tRepentogon:");
		bool launchingVanilla = _configuration->IsaacLaunchMode() == LAUNCH_MODE_VANILLA;
		if (launchingVanilla) {
			_logWindow.Log("\t\tRepentogon is disabled");
		} else {
			_logWindow.Log("\t\tRepentogon is enabled");
			_logWindow.Log("\t\tEnable Repentogon console window: %s", _configuration->RepentogonConsole() ? "yes" : "no");
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

		if (_configuration->HideWindow()) {
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

		if (_configuration->HideWindow()) {
			Show(true);
		}

		std::string desc;
		utils::ErrorCodeToString(exitCode, desc);

		if (exitCode && exitCode != LauncherInterface::LAUNCHER_EXIT_MODS_CHANGED &&
			exitCode != STATUS_CONTROL_C_EXIT) {
			_logWindow.LogWarn("Game exited with error code %#lx (%s)\n", exitCode, desc.c_str());
		} else {
			_logWindow.LogInfo("Game sucessfully exited\n");
		}

		EnableInterface(true);

		if (exitCode == LauncherInterface::LAUNCHER_EXIT_MODS_CHANGED) {
			RelaunchIsaac();
		}
	}

	void LauncherMainWindow::RelaunchIsaac() {
		LaunchIsaac();
	}

	void LauncherMainWindow::EnableInterface(bool enable) {
		_configurationBox->Enable(enable);
		_advancedOptionsButton->Enable(enable);
		_optionsBox->Enable(enable);
	}

	bool LauncherMainWindow::SelectIsaacExecutablePath() {
		LauncherWizard* wizard = new LauncherWizard(this, _installation, _configuration);
		wizard->AddPages(true);
		bool ok = wizard->Run();
		wizard->Destroy();

		IsaacInstallation const& isaacInstallation = _installation->GetIsaacInstallation();
		RepentogonInstallation const& repentogonInstallation = _installation->GetRepentogonInstallation();

		InstallationData const& mainData = isaacInstallation.GetMainInstallation();
		if (mainData.IsValid()) {
			_isaacFileText->SetForegroundColour(*wxBLACK);
			_isaacFileText->SetValue(mainData.GetExePath());
			_launchButton->Enable();
		} else {
			_isaacFileText->SetForegroundColour(*wxRED);
			_isaacFileText->SetValue("No valid Isaac installation specified");
			_logWindow.LogError("No valid Isaac installation provided, "
				"cannot start until a correct value is provided !");
			_launchButton->Disable();
		}

		if (repentogonInstallation.IsValid()) {
			InstallationData const& repData = isaacInstallation.GetRepentogonInstallation();
			_repentogonFileText->SetForegroundColour(*wxBLACK);
			_repentogonFileText->SetValue(repData.GetExePath());
		} else {
			_repentogonFileText->SetForegroundColour(*wxRED);
			_repentogonFileText->SetValue("No valid Repentogon installation found");
			_logWindow.LogWarn("No valid Repentogon installation found");
		}

		UpdateRepentogonOptionsFromInstallation();
		_updates->SetValue(_configuration->AutomaticUpdates());
		_unstableRepentogon->SetValue(_configuration->UnstableUpdates());

		return ok;
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

	void LauncherMainWindow::PreInit() {
/* #ifdef LAUNCHER_UNSTABLE
		wxMessageBox("You are running an unstable version of the REPENTOGON launcher.\n"
			"If you wish to run a stable version, use the \"Self-update (stable version)\" button",
			"Unstable launcher version", wxCENTER | wxICON_WARNING);
		LogWarn("Running an unstable version of the launcher");
#endif */
		_logWindow.Log("Welcome to the REPENTOGON Launcher (version %s)", Launcher::version);
		std::string currentDir = Filesystem::GetCurrentDirectory_();
		_logWindow.Log("Current directory is: %s", currentDir.c_str());
		_logWindow.Log("Using configuration file %s\n", LauncherConfiguration::GetConfigurationPath());

		SanitizeLauncherUpdate();

		LPSTR cli = GetCommandLineA();
		_logWindow.Log("Command line: %s", cli);

		OneTimeIsaacPathInitialization();
		InitializeOptions();
		UpdateRepentogonOptionsFromInstallation();
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
			_configuration->IsaacLaunchMode(LAUNCH_MODE_REPENTOGON);
		}

		std::string luaHeapSize = _configuration->LuaHeapSize();
		if (!luaHeapSize.empty()) {
			(void)std::remove_if(luaHeapSize.begin(), luaHeapSize.end(), [](char c) -> bool { return std::isspace(c); });
			std::cmatch match;
			if (!std::regex_match(luaHeapSize.c_str(), match, LuaHeapSizeRegex)) {
				_logWindow.LogWarn("Invalid value %s for %s field in configuration file. Ignoring\n",
					luaHeapSize.c_str(), c::Keys::luaHeapSize.c_str());
				_configuration->LuaHeapSize(c::Defaults::luaHeapSize);
			} else {
				_configuration->LuaHeapSize(luaHeapSize);
			}
		}

		// Validate LevelStage
		int levelStage = _configuration->Stage();
		if (levelStage < IsaacInterface::STAGE_NULL || levelStage > IsaacInterface::STAGE8) {
			_logWindow.LogWarn("Invalid value %d for %s field in configuration file. Overriding with default",
				levelStage, c::Keys::levelStage.c_str());
			_configuration->Stage(c::Defaults::levelStage);
			_configuration->StageType(c::Defaults::stageType);
		}

		// Validate StageType
		int stageType = _configuration->StageType();
		if (stageType < IsaacInterface::STAGETYPE_ORIGINAL || stageType > IsaacInterface::STAGETYPE_REPENTANCE_B) {
			_logWindow.LogWarn("Invalid value %d for %s field in configuration file. Overriding with default",
				stageType, c::Keys::stageType.c_str());
			_configuration->StageType(c::Defaults::stageType);
		}

		// Validate the combination of LevelStage+StageType
		if (levelStage > IsaacInterface::STAGE_NULL && !IsaacInterface::IsValidStage(levelStage, stageType)) {
			_logWindow.LogWarn("Invalid value %d for %s field associated with value %d "
				"for %s field in configuration file. Overriding with default", stageType, c::Keys::stageType.c_str(),
				levelStage, c::Keys::levelStage.c_str());
			_configuration->StageType(c::Defaults::stageType);
		}
	}

	void LauncherMainWindow::InitializeGUIFromOptions() {
		_configurationBox->GetWindowChild(WINDOW_BUTTON_SELECT_ISAAC)->Enable(!_configuration->IsaacExecutablePathHasCliOverride());

		_console->SetValue(_configuration->RepentogonConsole());
		_console->Enable(!_configuration->RepentogonConsoleHasCliOverride());

		_updates->SetValue(_configuration->AutomaticUpdates());
		_updates->Enable(!_configuration->AutomaticUpdatesHasCliOverride());

		_unstableRepentogon->SetValue(_configuration->UnstableUpdates());
		_unstableRepentogon->Enable(!_configuration->UnstableUpdatesHasCliOverride());
		_initialUnstableUpdates = _unstableRepentogon->GetValue();

		_luaHeapSize->SetValue(_configuration->LuaHeapSize());
		_luaHeapSize->Enable(!_configuration->LuaHeapSizeHasCliOverride());

		_luaDebug->SetValue(_configuration->LuaDebug());
		_luaDebug->Enable(!_configuration->LuaDebugHasCliOverride());

		_hideWindow->SetValue(_configuration->HideWindow());
		_hideWindow->Enable(!_configuration->HideWindowHasCliOverride());

		InitializeLevelSelectFromOptions();

		if (_configuration->IsaacLaunchMode() == LAUNCH_MODE_REPENTOGON) {
			_launchMode->SetValue(ComboBoxRepentogon);
		} else {
			_launchMode->SetValue(ComboBoxVanilla);
		}
		_launchMode->Enable(!_configuration->IsaacLaunchModeHasCliOverride());
	}

	void LauncherMainWindow::InitializeLevelSelectFromOptions() {
		int levelStage = _configuration->Stage();
		int stageType = _configuration->StageType();

		const char* stageName = IsaacInterface::GetStageName(levelStage, stageType);
		if (stageName) {
			_levelSelect->SetValue(wxString().Format("%s (%d.%d)", stageName, levelStage, stageType));
		} else {
			_levelSelect->SetSelection(0);
		}
		_levelSelect->Enable(!_configuration->StageHasCliOverride());
	}

	void LauncherMainWindow::UpdateLaunchButtonEnabledState() {
		if (_launchButton) {
			_launchButton->Enable(GetIsaacLaunchability() == ISAAC_LAUNCH_OK);
		}
	}

	void LauncherMainWindow::UpdateRepentogonOptionsFromInstallation() {
		const IsaacLaunchability launchability = GetIsaacLaunchability();
		if (launchability != ISAAC_LAUNCH_OK && _configuration->IsaacLaunchMode() == LAUNCH_MODE_REPENTOGON) {
			_logWindow.LogWarn("Disabling REPENTOGON start option: %s", GetIsaacLaunchabilityErrorMessage(launchability));
		}
		UpdateLaunchButtonEnabledState();
	}

	void LauncherMainWindow::ForceRepentogonUpdate(bool allowPreReleases) {
		_logWindow.Log("Forcibly updating Repentogon to the latest version");

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
		SetFocus();
		EnableInterface(true);

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

			_initialUnstableUpdates = _unstableRepentogon->GetValue();
			_canPromptOnUnstableSwitch = true;
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

	void LauncherMainWindow::AddLauncherConfigurationTextField(const char* intro,
		const char* buttonText, const char* emptyText, wxColour const& emptyColor,
		wxBoxSizer* sizer, wxTextCtrl** result, Launcher::Windows windowId) {

		wxStaticText* txt = new wxStaticText(_configurationBox, -1, intro, wxDefaultPosition);
		int width, height;
		txt->GetTextExtent("Repentogon executable", &width, &height);
		txt->SetMinSize(wxSize(width, height));
		sizer->Add(txt, 0, wxRIGHT, 5);

		wxButton* isaacSelectionButton = new wxButton(_configurationBox, windowId, buttonText);
		wxTextCtrl* textCtrl = new wxTextCtrl(_configurationBox, -1, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxTE_RICH);
		textCtrl->SetBackgroundColour(*wxWHITE);

		wxTextAttr textAttr = textCtrl->GetDefaultStyle();
		textCtrl->SetDefaultStyle(wxTextAttr(emptyColor));
		textCtrl->AppendText(emptyText);
		textCtrl->SetDefaultStyle(textAttr);

		wxSizerFlags flags = wxSizerFlags().Expand().Proportion(1);
		sizer->Add(textCtrl, flags);

		sizer->Add(isaacSelectionButton, 0, wxRIGHT, 5);

		if (result) {
			*result = textCtrl;
		}
	}

	bool LauncherMainWindow::PromptLegacyUninstall() {
		std::ostringstream s;
		s << "An unsupported build of Repentogon is currently installed and must be removed in order to upgrade to the latest Repentgon version.\n"
			"Proceed with its uninstallation?";
		wxMessageDialog modal(this, s.str(), "Uninstall legacy Repentogon installation?", wxYES_NO);
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

		case ADVANCED_EVENT_FORCE_REPENTOGON_UPDATE:
			ForceRepentogonUpdate(GetRepentogonUnstableUpdatesState());
			break;

		default:
			_logWindow.LogError("Unhandled result from ShowModal: %d", result);
			break;
		}
	}
}