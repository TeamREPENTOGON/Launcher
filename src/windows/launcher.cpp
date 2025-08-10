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
#include "shared/logger.h"
#include "shared/monitor.h"
#include "shared/launcher_update_checker.h"

#include "zip.h"
#include "zipint.h"

#ifdef min
#undef min
#endif
#include <launcher/modmanager.h>

wxBEGIN_EVENT_TABLE(Launcher::LauncherMainWindow, wxFrame)
EVT_COMBOBOX(Launcher::WINDOW_COMBOBOX_LEVEL, Launcher::LauncherMainWindow::OnLevelSelect)
EVT_COMBOBOX(Launcher::WINDOW_COMBOBOX_LAUNCH_MODE, Launcher::LauncherMainWindow::OnLaunchModeSelect)
EVT_TEXT(Launcher::WINDOW_COMBOBOX_LAUNCH_MODE, Launcher::LauncherMainWindow::OnLaunchModeSelect)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_CONSOLE, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_UPDATES, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_VANILLA_LUADEBUG, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_HIDE_WINDOW, Launcher::LauncherMainWindow::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES, Launcher::LauncherMainWindow::OnOptionSelected)
// EVT_TEXT(Launcher::WINDOW_TEXT_VANILLA_LUAHEAPSIZE, Launcher::LauncherMainWindow::OnCharacterWritten)
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

	LuaHeapSizeValidator::LuaHeapSizeValidator() : wxValidator() { }

	LuaHeapSizeValidator::LuaHeapSizeValidator(std::string* output) : wxValidator(), _output(output),
		_regex("([0-9]+)([KMG]?)") {

	}

	bool LuaHeapSizeValidator::TransferFromWindow() {
		wxTextCtrl* ctrl = dynamic_cast<wxTextCtrl*>(GetWindow());
		if (!ctrl) {
			return false;
		}

		*_output = ctrl->GetValue();
		return true;
	}

	bool LuaHeapSizeValidator::TransferToWindow() {
		wxTextCtrl* ctrl = dynamic_cast<wxTextCtrl*>(GetWindow());
		if (!ctrl) {
			return false;
		}

		ctrl->ChangeValue(*_output);
		return true;
	}

	bool LuaHeapSizeValidator::Validate(wxWindow*) {
		wxTextCtrl* ctrl = dynamic_cast<wxTextCtrl*>(GetWindow());
		wxString str = ctrl->GetValue();

		if (str.empty()) {
			ctrl->ChangeValue("0");
			return true;
		}

		std::cmatch match;
		bool result = std::regex_match((const char*)str, match, _regex);
		if (result) {
			return true;
		}

		if (str.size() == 1) {
			char c = str[0];
			if (c == 'K' || c == 'M' || c == 'G') {
				ctrl->ChangeValue("0");
				return true;
			}
		}

		return false;
	}

	LauncherMainWindow::LauncherMainWindow(Installation* installation, LauncherConfiguration* configuration) :
		wxFrame(nullptr, wxID_ANY, "REPENTOGON Launcher"),
		_installation(installation), _configuration(configuration),
		_logWindow(new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, -1),
			wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH)), _validator() {
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
		wxStaticText* repentogonExeText = new wxStaticText(_configurationBox, wxID_ANY, "Repentogon executable",wxDefaultPosition ,wxSize(150,20));
		_repentogonFileText = new wxTextCtrl(_configurationBox, wxID_ANY, "", wxDefaultPosition,
			wxDefaultSize, wxTE_READONLY);
		repentogonExeSizer->Add(repentogonExeText);
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

		_launchMode = new wxComboBox(launchModeBox, WINDOW_COMBOBOX_LAUNCH_MODE);
		_repentogonLaunchModeIdx = _launchMode->Append(ComboBoxRepentogon);
		_launchMode->Append(ComboBoxVanilla);
		_launchMode->SetValue(ComboBoxRepentogon);

		box->Add(_launchMode);

		launchModeBoxSizer->Add(box, 0, wxTOP | wxLEFT | wxRIGHT, 0);

		_launchButton = new wxButton(this, WINDOW_BUTTON_LAUNCH_BUTTON, "Launch game", wxDefaultPosition,wxSize(50,50));
		//launchModeBoxSizer->Add(_launchButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

		launchModeBoxSizer->Add(new wxStaticLine(launchModeBox), 0, wxBOTTOM, 0);

		_launchnmoddingSizer->Add(launchModeBoxSizer, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 0);
		_verticalSizer->Add(_launchButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
		_launchnmoddingSizer->AddSpacer(10);
	}

	void LauncherMainWindow::AddModdingOptions() {
		wxStaticBox* repentogonBox = new wxStaticBox(_optionsBox, -1, "Modding Options");
		wxStaticBoxSizer* repentogonBoxSizer = new wxStaticBoxSizer(repentogonBox, wxVERTICAL);

		wxButton* modman = new wxButton(repentogonBox, WINDOW_BUTTON_MODMAN_BUTTON, "Open Mod Manager", wxDefaultPosition, wxSize(200, 30));
		repentogonBoxSizer->Add(modman, 0, wxCENTER | wxBOTTOM, 5);
		_launchnmoddingSizer->Add(repentogonBoxSizer, 0, wxTOP | wxLEFT | wxRIGHT | wxBOTTOM, 0);
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
		vanillaBoxSizer->Add(_luaDebug, 0, wxLEFT | wxRIGHT, 5);
		vanillaBoxSizer->Add(new wxStaticLine(vanillaBox), 0, wxTOP | wxBOTTOM, 5);

		_validator.SetOutputVariable((std::string*)&_configuration->LuaHeapSize());
		wxSizer* heapSizeBox = new wxBoxSizer(wxHORIZONTAL);
		wxTextCtrl* heapSizeCtrl = new wxTextCtrl(vanillaBox, WINDOW_TEXT_VANILLA_LUAHEAPSIZE, "1024");
		heapSizeCtrl->SetValidator(_validator);
		_luaHeapSize = heapSizeCtrl;
		wxStaticText* heapSizeText = new wxStaticText(vanillaBox, -1, "Lua heap size (MB): ");
		heapSizeBox->Add(heapSizeText);
		heapSizeBox->Add(heapSizeCtrl);
		vanillaBoxSizer->Add(heapSizeBox, 0, wxLEFT | wxRIGHT, 20);
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
		if (std::regex_search(text, match, std::basic_regex("([0-9])\\.([0-9])"))) {
			int stage = std::stoi(match[1].str(), NULL, 0);
			int type = std::stoi(match[2].str(), NULL, 0);
			_configuration->Stage(stage);
			_configuration->StageType(type);
		} else if (!strcmp(text, "--")) {
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
	}

	/* void LauncherMainWindow::OnCharacterWritten(wxCommandEvent& event) {
		wxTextCtrl* ctrl = dynamic_cast<wxTextCtrl*>(event.GetEventObject());
		_options.luaHeapSize = std::stoi(ctrl->GetValue().c_str().AsChar());
	} */

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

	void LauncherMainWindow::LaunchIsaac() {
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
		if (_configuration->Stage()) {
			_logWindow.Log("\t\tStarting stage: %d.%d", _configuration->Stage(), _configuration->StageType());
		} else {
			_logWindow.Log("\t\tStarting stage: not selected");
		}
		_logWindow.Log("\t\tLua debug: %s", _configuration->LuaDebug() ? "yes" : "no");
		_logWindow.Log("\t\tLua heap size: %s", _configuration->LuaHeapSize().c_str());

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
			return;
		}

		strcpy(_exePath, pathStr.c_str());

		EnableInterface(false);

		if (_configuration->HideWindow()) {
			Show(false);
		}
		chained_futures::async(&::Launcher::Launch, &_logWindow, _exePath,
			launchingVanilla, _configuration
		).chain(std::bind_front(&LauncherMainWindow::OnIsaacCompleted, this));
	}

	void LauncherMainWindow::OnIsaacCompleted(int exitCode) {
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

		if (exitCode && exitCode != LauncherInterface::LAUNCHER_EXIT_MODS_CHANGED) {
			_logWindow.LogWarn("Game exited with error code %d\n", exitCode);
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

	void LauncherMainWindow::InitializeOptions() {
		SanitizeConfiguration();
		InitializeGUIFromOptions();
	}

	void LauncherMainWindow::SanitizeConfiguration() {
		namespace c = Configuration;
		LaunchMode mode = _configuration->IsaacLaunchMode();
		if (mode != LAUNCH_MODE_REPENTOGON && mode != LAUNCH_MODE_VANILLA) {
			_logWindow.LogWarn("Invalid value %d for %s field in configuration file. Overriding with default", mode, c::Keys::launchMode.c_str());
			if (_installation->GetRepentogonInstallation().IsValid()) {
				_configuration->IsaacLaunchMode(LAUNCH_MODE_REPENTOGON);
			} else {
				_configuration->IsaacLaunchMode(LAUNCH_MODE_VANILLA);
			}
		}

		std::string luaHeapSize = _configuration->LuaHeapSize();
		if (!luaHeapSize.empty()) {
			(void)std::remove_if(luaHeapSize.begin(), luaHeapSize.end(), [](char c) -> bool { return std::isspace(c); });
			std::regex r("([0-9]+)([KMG]?)");
			std::cmatch match;
			bool hasMatch = std::regex_match(luaHeapSize.c_str(), match, r);

			if (!hasMatch) {
				_logWindow.LogWarn("Invalid value %s for %s field in configuration file. Ignoring\n",
					luaHeapSize.c_str(), c::Keys::luaHeapSize.c_str());
				_configuration->LuaHeapSize("");
			} else {
				_configuration->LuaHeapSize(luaHeapSize);
			}
		}

		int levelStage = _configuration->Stage();
		if (levelStage < IsaacInterface::STAGE_NULL || levelStage > IsaacInterface::STAGE8) {
			_logWindow.LogWarn("Invalid value %d for %s field in configuration file. Overriding with default",
				levelStage, c::Keys::levelStage.c_str());
			_configuration->Stage(c::Defaults::levelStage);
			_configuration->StageType(c::Defaults::stageType);
		}

		int stageType = _configuration->StageType();
		if (stageType < IsaacInterface::STAGETYPE_ORIGINAL || stageType > IsaacInterface::STAGETYPE_REPENTANCE_B) {
			_logWindow.LogWarn("Invalid value %d for %s field in configuration file. Overriding with default",
				stageType, c::Keys::stageType.c_str());
			_configuration->StageType(c::Defaults::stageType);
		}

		if (stageType == IsaacInterface::STAGETYPE_GREEDMODE) {
			_logWindow.LogWarn("Value 3 (Greed mode) for %s field in configuration file is deprecated since Repentance."
				"Overriding with default", c::Keys::stageType.c_str());
			_configuration->StageType(c::Defaults::stageType);
		}

		// Validate stage type for Chapter 4.
		if (levelStage == IsaacInterface::STAGE4_1 || levelStage == IsaacInterface::STAGE4_2) {
			if (stageType == IsaacInterface::STAGETYPE_REPENTANCE_B) {
				_logWindow.LogWarn("Invalid value %d for %s field associated with value %d "
					"for %s field in configuration file. Overriding with default", stageType, c::Keys::levelStage.c_str(),
					stageType, c::Keys::stageType.c_str());
				_configuration->StageType(c::Defaults::stageType);
			}
		}

		// Validate stage type for Chapters > 4.
		if (levelStage >= IsaacInterface::STAGE4_3 && levelStage <= IsaacInterface::STAGE8) {
			if (stageType != IsaacInterface::STAGETYPE_ORIGINAL) {
				_logWindow.LogWarn("Invalid value %d for %s field associated with value %d "
					"for %s field in configuration file. Overriding with default", levelStage, c::Keys::levelStage.c_str(),
					stageType, c::Keys::stageType.c_str());
				_configuration->StageType(c::Defaults::stageType);
			}
		}
	}

	void LauncherMainWindow::InitializeGUIFromOptions() {
		_console->SetValue(_configuration->RepentogonConsole());
		_updates->SetValue(_configuration->AutomaticUpdates());
		_unstableRepentogon->SetValue(_configuration->UnstableUpdates());
		_initialUnstableUpdates = _unstableRepentogon->GetValue();

		_luaHeapSize->SetValue(_configuration->LuaHeapSize());

		_luaDebug->SetValue(_configuration->LuaDebug());
		_hideWindow->SetValue(_configuration->HideWindow());
		InitializeLevelSelectFromOptions();
		if (_configuration->IsaacLaunchMode() == LAUNCH_MODE_REPENTOGON) {
			_launchMode->SetValue(ComboBoxRepentogon);
		} else {
			_launchMode->SetValue(ComboBoxVanilla);
		}
	}

	void LauncherMainWindow::InitializeLevelSelectFromOptions() {
		int level = _configuration->Stage(), type = _configuration->StageType();
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

	void LauncherMainWindow::UpdateRepentogonOptionsFromInstallation() {
		RepentogonInstallation const& repentogonInstallation = _installation->GetRepentogonInstallation();
		IsaacInstallation const& isaacInstallation = _installation->GetIsaacInstallation();

		bool isValid = repentogonInstallation.IsValid();
		bool isIncompatible = !isaacInstallation.GetRepentogonInstallation().IsCompatibleWithRepentogon();

		if (!isValid || isIncompatible) {
			if (_repentogonLaunchModeIdx != -1) {
				if (!isValid) {
					_logWindow.LogWarn("Removing Repentogon start option as the Repentogon installation is broken\n");
				}

				if (isIncompatible) {
					_logWindow.LogWarn("Removing Repentogon start option as the Isaac executable is not compatible\n");
				}

				_launchMode->Delete(_repentogonLaunchModeIdx);
				_repentogonLaunchModeIdx = -1;
			}

			_launchMode->SetValue(ComboBoxVanilla);
		} else {
			if (_repentogonLaunchModeIdx == -1) {
				_repentogonLaunchModeIdx = _launchMode->Append(ComboBoxRepentogon);
			}

			_launchMode->SetValue(ComboBoxRepentogon);

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
		sizer->Add(new wxStaticText(_configurationBox, -1, intro, wxDefaultPosition, wxSize(150, 20)));
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