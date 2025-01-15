#pragma once

#include <mutex>
#include <optional>

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
	#include <wx/wx.h>
#endif

#include "curl/curl.h"
#include "launcher/installation.h"
#include "launcher/installation_manager.h"
#include "launcher/launcher.h"
#include "launcher/self_update.h"
#include "launcher/repentogon_updater.h"
#include "launcher/self_updater/launcher_update_manager.h"
#include "rapidjson/document.h"
#include "shared/loggable_gui.h"

namespace Launcher {
	enum Windows {
		WINDOW_COMBOBOX_LEVEL,
		WINDOW_COMBOBOX_LAUNCH_MODE,
		WINDOW_CHECKBOX_REPENTOGON_CONSOLE,
		WINDOW_CHECKBOX_REPENTOGON_UPDATES,
		WINDOW_CHECKBOX_REPENTOGON_UNSTABLE_UPDATES,
		WINDOW_CHECKBOX_VANILLA_LUADEBUG,
		WINDOW_TEXT_VANILLA_LUAHEAPSIZE,
		WINDOW_BUTTON_LAUNCH_BUTTON,
		WINDOW_BUTTON_SELECT_ISAAC,
		WINDOW_BUTTON_SELECT_REPENTOGON_FOLDER,
		WINDOW_BUTTON_ADVANCED_OPTIONS
	};

	class App : public wxApp {
	public:
		bool OnInit() override;
		int OnExit() override;
		void ParseCommandLine();
	};

	class AdvancedOptionsWindow;

	class MainFrame : public wxFrame, public ILoggableGUI {
	public:
		friend class AdvancedOptionsWindow;

		enum AdvancedOptionsEvents {
			ADVANCED_EVENT_NONE,
			ADVANCED_EVENT_FORCE_REPENTOGON_UPDATE,
			ADVANCED_EVENT_FORCE_REPENTOGON_UNSTABLE_UPDATE,
			ADVANCED_EVENT_FORCE_LAUNCHER_UPDATE,
			ADVANCED_EVENT_FORCE_LAUNCHER_UNSTABLE_UPDATE
		};

		MainFrame();
		~MainFrame();

		void Log(const char* prefix, bool nl, const char* fmt, ...);
		void LogNoNL(const char* fmt, ...);
		void Log(const char* fmt, ...);
		void LogInfo(const char* fmt, ...);
		void LogWarn(const char* fmt, ...);
		void LogError(const char* fmt, ...);
		void PostInit();

	private:
		Installation _installation;

		/* Window building. */
		void AddLauncherConfigurationOptions();
		void AddLaunchOptions();
		void AddRepentogonOptions();
		void AddVanillaOptions();

		void AddLauncherConfigurationTextField(const char* intro, 
			const char* buttonText, const char* emptyText, wxColour const& emptyColor, 
			wxBoxSizer* sizer, wxTextCtrl** result, Launcher::Windows windowId);

		bool PromptLauncherUpdate(std::string const& version, std::string const& url);

		/* Initialize the IsaacOptions structure. */
		void InitializeOptions();
		/* Initialize GUI components from the IsaacOptions structure. */
		void InitializeGUIFromOptions();
		/* Helper to initialize the level selection field from the IsaacOptions structure. */
		void InitializeLevelSelectFromOptions();
		void UpdateRepentogonOptionsFromInstallation();

		/* Event handlers. */
		void OnIsaacSelectClick(wxCommandEvent& event);
		void OnSelectRepentogonFolderClick(wxCommandEvent& event);
		void OnFileSelected(std::string const& path, wxColor const& emptyColor, wxTextCtrl* ctrl, 
			const char* emptyText);
		void OnLevelSelect(wxCommandEvent& event);
		void OnLauchModeSelect(wxCommandEvent& event);
		void OnCharacterWritten(wxCommandEvent& event);
		void OnOptionSelected(wxCommandEvent& event);
		void OnAdvancedOptionsClick(wxCommandEvent& event);

		void ForceRepentogonUpdate(bool allowPreReleases);

		void Launch(wxCommandEvent& event);

		/* Prompt the user for the folder containing an Isaac installation. */
		std::string PromptIsaacInstallation();

		/* Prompt the user for a Repentogon installation. This is useful if we 
		 * don't detect a valid Repentogon installation. 
		 * 
		 * Return true if the user wants a download, false otherwise.
		 */
		bool PromptRepentogonInstallation();

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

		bool InitializeIsaacExecutablePath(bool shouldPrompt);
		void HandleLauncherUpdates(bool allowDrafts);
		bool HandleIsaacExecutableSelection(std::string const& path);

		void PostInitHandleRepentogon();

		void EnableInterface(bool enable);

		IsaacOptions _options;
		wxStaticBoxSizer* _optionsSizer;
		wxStaticBoxSizer* _configurationSizer;
		wxCheckBox* _console;
		wxCheckBox* _updates;
		wxCheckBox* _unstableRepentogon;
		wxCheckBox* _luaDebug;
		wxComboBox* _levelSelect;
		wxComboBox* _launchMode;
		wxTextCtrl* _luaHeapSize;
		wxStaticBox* _optionsBox;
		wxStaticBox* _configurationBox;
		wxStaticBox* _repentogonOptions;
		wxTextCtrl* _isaacFileText;
		wxTextCtrl* _repentogonInstallFolderText;
		wxButton* _launchButton;
		wxButton* _advancedOptionsButton;
		int _repentogonLaunchModeIdx = -1;
		AdvancedOptionsEvents _advancedEvent = ADVANCED_EVENT_NONE;

		std::mutex _logMutex;
		wxTextCtrl* _logWindow;
		/* Log string used in CheckUpdates to indicate which tool is being checked. */
		std::string _currentUpdate;

		wxDECLARE_EVENT_TABLE();
	};
}