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
#include "launcher/isaac.h"
#include "launcher/log_helpers.h"
#include "launcher/launcher_self_update.h"
#include "launcher/windows/launcher.h"
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

wxBEGIN_EVENT_TABLE(Launcher::MainFrame, wxFrame)
EVT_COMBOBOX(Launcher::WINDOW_COMBOBOX_LEVEL, Launcher::MainFrame::OnLevelSelect)
EVT_COMBOBOX(Launcher::WINDOW_COMBOBOX_LAUNCH_MODE, Launcher::MainFrame::OnLauchModeSelect)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_CONSOLE, Launcher::MainFrame::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_UPDATES, Launcher::MainFrame::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_VANILLA_LUADEBUG, Launcher::MainFrame::OnOptionSelected)
EVT_CHECKBOX(Launcher::WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES, Launcher::MainFrame::OnOptionSelected)
// EVT_TEXT(Launcher::WINDOW_TEXT_VANILLA_LUAHEAPSIZE, Launcher::MainFrame::OnCharacterWritten)
EVT_BUTTON(Launcher::WINDOW_BUTTON_LAUNCH_BUTTON, Launcher::MainFrame::Launch)
EVT_BUTTON(Launcher::WINDOW_BUTTON_SELECT_ISAAC, Launcher::MainFrame::OnIsaacSelectClick)
// EVT_BUTTON(Launcher::WINDOW_BUTTON_SELECT_REPENTOGON_FOLDER, Launcher::MainFrame::OnSelectRepentogonFolderClick)
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

	bool LuaHeapSizeValidator::Validate(wxWindow* parent) {
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

	MainFrame::MainFrame(Installation* installation, LauncherConfiguration* configuration) :
		wxFrame(nullptr, wxID_ANY, "REPENTOGON Launcher"),
		_installation(installation), _configuration(configuration),
		_logWindow(new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, -1),
			wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH)), _validator() {
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
		_configuration->Write();
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

	void MainFrame::OnIsaacSelectClick(wxCommandEvent& event) {
		SelectIsaacExecutablePath();
	}

	/* void MainFrame::OnSelectRepentogonFolderClick(wxCommandEvent& event) {
		wxDirDialog dialog(this, "Please select the folder in which to install Repentogon", wxEmptyString,
			0, wxDefaultPosition, wxDefaultSize, "Select Repentogon installation folder");
		dialog.ShowModal();

		std::string path = dialog.GetPath().ToStdString();
		OnFileSelected(path, NoRepentogonInstallationFolderColor, _repentogonInstallFolderText, NoRepentogonInstallationFolderText);
	} */

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
			_configuration->Stage(stage);
			_configuration->StageType(type);
		} else if (!strcmp(text, "--")) {
			_configuration->Stage(0);
			_configuration->StageType(0);
		}
	}

	void MainFrame::OnLauchModeSelect(wxCommandEvent& event) {
		wxComboBox* box = dynamic_cast<wxComboBox*>(event.GetEventObject());
		if (box->GetValue() == "Vanilla") {
			_configuration->IsaacLaunchMode(LAUNCH_MODE_VANILLA);
		} else {
			_configuration->IsaacLaunchMode(LAUNCH_MODE_REPENTOGON);
		}
	}

	/* void MainFrame::OnCharacterWritten(wxCommandEvent& event) {
		wxTextCtrl* ctrl = dynamic_cast<wxTextCtrl*>(event.GetEventObject());
		_options.luaHeapSize = std::stoi(ctrl->GetValue().c_str().AsChar());
	} */

	void MainFrame::OnOptionSelected(wxCommandEvent& event) {
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
			break;

		case WINDOW_CHECKBOX_VANILLA_LUADEBUG:
			_configuration->LuaDebug(box->GetValue());
			break;

		default:
			return;
		}
	}

	void MainFrame::Launch(wxCommandEvent& event) {
		_logWindow.Log("Launching the game with the following options:");
		_logWindow.Log("\tRepentogon:");
		if (_configuration->IsaacLaunchMode() == LAUNCH_MODE_VANILLA) {
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
		_logWindow.Log("\t\tLua heap size: %s", _configuration->LuaHeapSize());

		_configuration->Write();
		::Launcher::Launch(&_logWindow, _isaacFileText->GetValue().c_str().AsChar(), _installation->GetRepentogonInstallation().IsLegacy(), _configuration);
	}

	void MainFrame::EnableInterface(bool enable) {
		_logWindow.LogWarn("Interface freezing / unfreezing when launching the game is not yet implemented\n");
	}

	bool MainFrame::SelectIsaacExecutablePath() {
		LauncherWizard* wizard = new LauncherWizard(_installation);
		wizard->AddPages(true);
		bool ok = wizard->Run();
		if (ok) {

		}
		wizard->Destroy();
		return ok;
	}

	bool MainFrame::SanityCheckLauncherUpdate() {
		return !Filesystem::FileExists(Comm::UnpackedArchiveName);
	}

	void MainFrame::SanitizeLauncherUpdate() {
		if (!SanityCheckLauncherUpdate()) {
			_logWindow.LogWarn("Found launcher update file %s in the current folder."
				"This may indicate a broken update. Deleting it.\n", Comm::UnpackedArchiveName);
			Filesystem::RemoveFile(Comm::UnpackedArchiveName);
		}
	}

	void MainFrame::OneTimeIsaacPathInitialization() {
		std::string const& isaacPath = _installation->GetIsaacInstallation().GetExePath();
		if (!isaacPath.empty())
			_isaacFileText->SetValue(_installation->GetIsaacInstallation().GetExePath());
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

		SanitizeLauncherUpdate();

		LPSTR cli = GetCommandLineA();
		_logWindow.Log("Command line: %s", cli);

		OneTimeIsaacPathInitialization();
		InitializeOptions();
		UpdateRepentogonOptionsFromInstallation();
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
		SanitizeConfiguration();
		InitializeGUIFromOptions();
	}

	void MainFrame::SanitizeConfiguration() {
		namespace c = Configuration;
		LaunchMode mode = _configuration->IsaacLaunchMode();
		if (mode != LAUNCH_MODE_REPENTOGON && mode != LAUNCH_MODE_VANILLA) {
			_logWindow.LogWarn("Invalid value %d for %s field in configuration file. Overriding with default", mode, c::Keys::launchMode.c_str());
			if (_installation->GetRepentogonInstallation().IsValid(false)) {
				_configuration->IsaacLaunchMode(LAUNCH_MODE_REPENTOGON);
			} else {
				_configuration->IsaacLaunchMode(LAUNCH_MODE_VANILLA);
			}
		}

		std::string luaHeapSize = _configuration->LuaHeapSize();
		(void)std::remove_if(luaHeapSize.begin(), luaHeapSize.end(), [](char c) -> bool { return c == ' '; });
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

	void MainFrame::InitializeGUIFromOptions() {
		_console->SetValue(_configuration->RepentogonConsole());
		_updates->SetValue(_configuration->AutomaticUpdates());
		_unstableRepentogon->SetValue(_configuration->UnstableUpdates());

		_luaHeapSize->SetValue(_configuration->LuaHeapSize());

		_luaDebug->SetValue(_configuration->LuaDebug());
		InitializeLevelSelectFromOptions();
		if (_configuration->IsaacLaunchMode() == LAUNCH_MODE_REPENTOGON) {
			_launchMode->SetValue("Repentogon");
		} else {
			_launchMode->SetValue("Vanilla");
		}
	}

	void MainFrame::InitializeLevelSelectFromOptions() {
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

	void MainFrame::UpdateRepentogonOptionsFromInstallation() {
		RepentogonInstallation const& repentogonInstallation = _installation->GetRepentogonInstallation();
		IsaacInstallation const& isaacInstallation = _installation->GetIsaacInstallation();

		if (!repentogonInstallation.IsValid(false) || !isaacInstallation.IsCompatibleWithRepentogon()) {
			if (_repentogonLaunchModeIdx != -1) {
				_logWindow.LogWarn("Removing Repentogon start option due to legacy installation\n");
				_launchMode->Delete(_repentogonLaunchModeIdx);
				_repentogonLaunchModeIdx = -1;
			}

			_launchMode->SetValue("Vanilla");
		} else {
			if (_repentogonLaunchModeIdx == -1) {
				_repentogonLaunchModeIdx = _launchMode->Append("Repentogon");
			}

			_launchMode->SetValue("Repentogon");

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

	void MainFrame::ForceLauncherUpdate(bool allowUnstable) {
		_logWindow.Log("Performing self-update (forcibly triggered)...");
		if (Launcher::CheckForSelfUpdate(allowUnstable)) {
			_logWindow.Log("No update available.");
		} else {
			_logWindow.LogError("Failed to launch self-updater executable\n");
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
			ForceLauncherUpdate(true);
			break;

		case ADVANCED_EVENT_FORCE_LAUNCHER_UPDATE:
			ForceLauncherUpdate(false);
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
}