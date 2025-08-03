#pragma once

#include <mutex>
#include <optional>
#include <regex>
#include <string>

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
	#include <wx/wx.h>
#endif

#include "curl/curl.h"
#include "launcher/installation.h"
#include "launcher/launcher_configuration.h"
#include "launcher/repentogon_installer.h"
#include "launcher/widgets/text_ctrl_log_widget.h"
#include "launcher/windows/repentogon_installer.h"
#include "rapidjson/document.h"
#include "shared/loggable_gui.h"

namespace Launcher {
	enum Windows {
		WINDOW_COMBOBOX_LEVEL,
		WINDOW_COMBOBOX_LAUNCH_MODE,
		WINDOW_CHECKBOX_REPENTOGON_CONSOLE,
		WINDOW_CHECKBOX_REPENTOGON_UPDATES,
		WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES,
		WINDOW_CHECKBOX_HIDE_WINDOW,
		WINDOW_CHECKBOX_VANILLA_LUADEBUG,
		WINDOW_TEXT_VANILLA_LUAHEAPSIZE,
		WINDOW_BUTTON_LAUNCH_BUTTON,
		WINDOW_BUTTON_SELECT_ISAAC,
		WINDOW_BUTTON_SELECT_REPENTOGON_FOLDER,
		WINDOW_BUTTON_ADVANCED_OPTIONS
	};

	class AdvancedOptionsWindow;

	class LuaHeapSizeValidator : public wxValidator {
	public:
		LuaHeapSizeValidator();
		LuaHeapSizeValidator(std::string* output);

		inline void SetOutputVariable(std::string* output) {
			_output = output;
		}

		virtual bool TransferFromWindow() override;
		virtual bool TransferToWindow() override;
		virtual bool Validate(wxWindow* parent) override;

	private:
		std::string* _output;
		std::regex _regex;
	};

	class LauncherMainWindow : public wxFrame {
	public:
		friend class AdvancedOptionsWindow;

		enum AdvancedOptionsEvents {
			ADVANCED_EVENT_NONE,
			ADVANCED_EVENT_FORCE_REPENTOGON_UPDATE,
			ADVANCED_EVENT_FORCE_REPENTOGON_UNSTABLE_UPDATE,
			ADVANCED_EVENT_FORCE_LAUNCHER_UPDATE,
			ADVANCED_EVENT_FORCE_LAUNCHER_UNSTABLE_UPDATE
		};

		LauncherMainWindow(Installation* installation, LauncherConfiguration* configuration);
		~LauncherMainWindow();

		/* Load the configuration file and update the options accordingly.
		 *
		 * If no configuration is found, perform a "one-time setup" to get the
		 * location of the Isaac installation.
		 */
		void PreInit();

		void EnableInterface(bool enable);

		/* The window must has been initialized. */
		inline bool GetRepentogonUnstableUpdatesState() const {
			return _unstableRepentogon->GetValue();
		}

	private:
		/* Window building. */
		void AddLauncherConfigurationOptions();
		void AddLaunchOptions();
		void AddRepentogonOptions();
		void AddVanillaOptions();

		void AddLauncherConfigurationTextField(const char* intro,
			const char* buttonText, const char* emptyText, wxColour const& emptyColor,
			wxBoxSizer* sizer, wxTextCtrl** result, Launcher::Windows windowId);

		/* Initialize the IsaacOptions structure. */
		void InitializeOptions();
		void SanitizeConfiguration();
		/* Initialize GUI components from the IsaacOptions structure. */
		void InitializeGUIFromOptions();
		/* Helper to initialize the level selection field from the IsaacOptions structure. */
		void InitializeLevelSelectFromOptions();
		void UpdateRepentogonOptionsFromInstallation();

		/* Event handlers. */
		void OnIsaacSelectClick(wxCommandEvent& event);
		// void OnSelectRepentogonFolderClick(wxCommandEvent& event);
		void OnFileSelected(std::string const& path, wxColor const& emptyColor, wxTextCtrl* ctrl,
			const char* emptyText);
		void OnLevelSelect(wxCommandEvent& event);
		void OnLaunchModeSelect(wxCommandEvent& event);
		// void OnCharacterWritten(wxCommandEvent& event);
		void OnOptionSelected(wxCommandEvent& event);
		void OnAdvancedOptionsClick(wxCommandEvent& event);

		void OnRepentogonUnstableStateSwitched();

		void OnForceUpdateCompleted(std::shared_ptr<RepentogonInstallerFrame> ptr);

		void ForceRepentogonUpdate(bool allowPreReleases);
		void ForceLauncherUpdate(bool allowPreReleases);

		void Launch(wxCommandEvent& event);

		void OneTimeIsaacPathInitialization();

		/* Prompt the user for the folder containing an Isaac installation. */
		std::string PromptIsaacInstallation();

		bool PromptBoolean(wxString const& message, wxString const& shortMessage);

		/* Prompt the user for removal of a legacy installation.
		 *
		 * If the backend detects the presence of dsound.dll, this means we are
		 * dealing with a legacy installation of Repentogon. Because the game
		 * will load the DLL if it remains next to it, prompt the user on whether
		 * they want to remove all Repentogon related DLLs in the Isaac folder.
		 */
		bool PromptLegacyUninstall();

		/* Check whether there is a launcher update file present in the current
		 * folder.
		 */
		bool SanityCheckLauncherUpdate();
		void SanitizeLauncherUpdate();

		bool SelectIsaacExecutablePath();

		void LaunchIsaac();
		void OnIsaacCompleted(int result);
		void RelaunchIsaac();

		wxStaticBoxSizer* _optionsSizer;
		wxStaticBoxSizer* _configurationSizer;
		wxSizer* _verticalSizer;
		wxCheckBox* _console;
		wxCheckBox* _updates;
		wxCheckBox* _unstableRepentogon;
		wxCheckBox* _luaDebug;
		wxCheckBox* _hideWindow;
		wxComboBox* _levelSelect;
		wxComboBox* _launchMode;
		wxTextCtrl* _luaHeapSize;
		wxStaticBox* _optionsBox;
		wxStaticBox* _configurationBox;
		wxStaticBox* _repentogonOptions;
		wxTextCtrl* _isaacFileText;
		wxTextCtrl* _repentogonFileText;
		wxButton* _launchButton;
		wxButton* _advancedOptionsButton;
		int _repentogonLaunchModeIdx = -1;
		AdvancedOptionsEvents _advancedEvent = ADVANCED_EVENT_NONE;
		LuaHeapSizeValidator _validator;
		bool _initialUnstableUpdates = false;
		bool _canPromptOnUnstableSwitch = true;
		char* _exePath = nullptr;

		wxTextCtrlLog _logWindow;
		Installation* _installation;
		LauncherConfiguration* _configuration;
		/* Log string used in CheckUpdates to indicate which tool is being checked. */
		std::string _currentUpdate;

		wxDECLARE_EVENT_TABLE();
	};
}