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
		WINDOW_CHECKBOX_STEALTH_MODE,
		WINDOW_CHECKBOX_VANILLA_LUADEBUG,
		WINDOW_TEXT_VANILLA_LUAHEAPSIZE,
		WINDOW_BUTTON_LAUNCH_BUTTON,
		WINDOW_BUTTON_SELECT_ISAAC,
		WINDOW_BUTTON_SELECT_REPENTOGON_FOLDER,
		WINDOW_BUTTON_ADVANCED_OPTIONS,
		WINDOW_BUTTON_ADVANCED_MOD_OPTIONS,
		WINDOW_BUTTON_MODMAN_BUTTON,
		WINDOW_BUTTON_CHECKLOGS_BUTTON,
		WINDOW_BUTTON_CHANGEOPTIONS_BUTTON
	};

	class AdvancedOptionsWindow;
	class AdvancedModOptionsWindow;

	class LauncherMainWindow : public wxFrame {
	public:
		friend class AdvancedOptionsWindow;
		friend class AdvancedModOptionsWindow;
		friend class CheckLogsWindow;

		enum AdvancedOptionsEvents {
			ADVANCED_EVENT_NONE,
			ADVANCED_EVENT_FORCE_REPENTOGON_UPDATE,
			ADVANCED_EVENT_FORCE_REPENTOGON_UNSTABLE_UPDATE,
			ADVANCED_EVENT_FORCE_LAUNCHER_UPDATE,
			ADVANCED_EVENT_FORCE_LAUNCHER_UNSTABLE_UPDATE,
			ADVANCED_EVENT_REINSTALL
		};
		
		enum CheckLogsEvents {
			CHECKLOGS_EVENT_NONE,
			CHECKLOGS_EVENT_LAUNCHERLOG,
			CHECKLOGS_EVENT_RGONLOG,
			CHECKLOGS_EVENT_GAMELOG,
			
			CHECKLOGS_EVENT_LAUNCHERLOG_LOCATE,
			CHECKLOGS_EVENT_RGONLOG_LOCATE,
			CHECKLOGS_EVENT_GAMELOG_LOCATE,

			CHECKLOGS_EVENT_LAUNCHERLOG_COPY,
			CHECKLOGS_EVENT_RGONLOG_COPY,
			CHECKLOGS_EVENT_GAMELOG_COPY
		};

		enum IsaacLaunchability {
			ISAAC_LAUNCH_UNKNOWN,
			ISAAC_LAUNCH_OK,
			ISAAC_LAUNCH_VANILLA_INVALID,
			ISAAC_LAUNCH_REPENTOGON_INVALID,
			ISAAC_LAUNCH_REPENTOGON_INCOMPATIBLE,
		};

		LauncherMainWindow(Installation* installation, LauncherConfiguration* configuration);
		~LauncherMainWindow();

		/* Load the configuration file and update the options accordingly.
		 *
		 * If no configuration is found, perform a "one-time setup" to get the
		 * location of the Isaac installation.
		 */
		void Init();

		void EnableInterface(bool enable);

		/* The window must has been initialized. */
		inline bool GetRepentogonUnstableUpdatesState() const {
			return _unstableRepentogon->GetValue();
		}

	private:
		/* Window building. */
		void AddLauncherConfigurationOptions();
		void AddLaunchOptions(const wxSizerFlags& sizerFlags);
		void AddRepentogonOptions(const wxSizerFlags& sizerFlags);
		void AddModdingOptions(const wxSizerFlags& sizerFlags);
		void AddOptionsConfigOptions(const wxSizerFlags& sizerFlags); //lol

		/* Initialize the IsaacOptions structure. */
		void InitializeOptions();
		void SanitizeConfiguration();
		/* Initialize GUI components from the IsaacOptions structure. */
		void InitializeGUIFromOptions();
		/* Helper to initialize the level selection field from the IsaacOptions structure. */
		// void InitializeLevelSelectFromOptions();
		void UpdateLaunchButtonEnabledState();
		void UpdateRepentogonOptionsFromInstallation();

		/* Event handlers. */
		void OnIsaacSelectClick(wxCommandEvent& event);
		// void OnSelectRepentogonFolderClick(wxCommandEvent& event);
		void OnFileSelected(std::string const& path, wxColor const& emptyColor, wxTextCtrl* ctrl,
			const char* emptyText);
		void OnLevelSelect(wxCommandEvent& event);
		void OnLaunchModeSelect(wxCommandEvent& event);
		void OnOptionSelected(wxCommandEvent& event);
		void OnAdvancedOptionsClick(wxCommandEvent& event);
		void OnAdvancedModOptionsClick(wxCommandEvent& event);
		void OnCheckLogsClick(wxCommandEvent& event);

		void OnRepentogonUnstableStateSwitched();

		void OnModManagerButtonPressed(wxCommandEvent& event);
		void OnChangeOptions(wxCommandEvent& event);

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

		bool SelectIsaacExecutablePath();
		void RefreshGUIFromInstallation();

		/* Attempts an "automatic" Isaac launch, if the current cli/configurations permit it.
		 * Intended to be called on launcher initialization, before showing the window.
		 * Returns true only on a successful auto-launch.
		 */
		bool TryAutoLaunch();

		IsaacLaunchability GetIsaacLaunchability();
		const char* GetIsaacLaunchabilityErrorMessage(const IsaacLaunchability state);

		bool LaunchIsaac(bool relaunch = false);
		void OnIsaacCompleted(DWORD result);
		void RelaunchIsaac();

		wxStaticBoxSizer* _optionsSizer = nullptr;
		wxFlexGridSizer* _optionsGridSizer = nullptr;
		wxStaticBoxSizer* _configurationSizer = nullptr;
		wxSizer* _verticalSizer = nullptr;
		wxCheckBox* _console = nullptr;
		wxCheckBox* _updates = nullptr;
		wxCheckBox* _unstableRepentogon = nullptr;
		// wxCheckBox* _hideWindow;
		wxCheckBox* _stealthMode = nullptr;
		// wxComboBox* _levelSelect;
		wxComboBox* _launchMode = nullptr;
		wxStaticBox* _optionsBox = nullptr;
		wxStaticBox* _configurationBox = nullptr;
		wxStaticBox* _repentogonOptions = nullptr;
		wxTextCtrl* _isaacFileText = nullptr;
		wxTextCtrl* _repentogonFileText = nullptr;
		wxButton* _launchButton = nullptr;
		wxButton* _advancedOptionsButton = nullptr;
		wxButton* _advancedModOptionsButton = nullptr;
		AdvancedOptionsEvents _advancedEvent = ADVANCED_EVENT_NONE;
		char* _exePath = nullptr;

		wxTextCtrlLog _logWindow;
		Installation* _installation;
		LauncherConfiguration* _configuration;
		/* Log string used in CheckUpdates to indicate which tool is being checked. */
		std::string _currentUpdate;
		bool attemptedpatchfix = false;

		wxDECLARE_EVENT_TABLE();
	};
}