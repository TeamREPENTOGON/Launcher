#pragma once

#include <mutex>
#include <optional>

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
	#include <wx/wx.h>
	#include <wx/gbsizer.h>
#endif

#include "curl/curl.h"
#include "launcher/filesystem.h"
#include "launcher/launcher.h"
#include "launcher/updater.h"
#include "rapidjson/document.h"

namespace Launcher {
	class MainFrame;

	enum Windows {
		WINDOW_COMBOBOX_LEVEL,
		WINDOW_COMBOBOX_LAUNCH_MODE,
		WINDOW_CHECKBOX_REPENTOGON_CONSOLE,
		WINDOW_CHECKBOX_REPENTOGON_UPDATES,
		WINDOW_CHECKBOX_VANILLA_LUADEBUG,
		WINDOW_TEXT_VANILLA_LUAHEAPSIZE,
		WINDOW_BUTTON_LAUNCH_BUTTON,
		WINDOW_BUTTON_FORCE_UPDATE,
		WINDOW_BUTTON_SELECT_ISAAC,
		WINDOW_BUTTON_SELF_UPDATE
	};

	class App : public wxApp {
	public:
		bool OnInit() override;
	};

	class MainFrame : public wxFrame {
	public:
		MainFrame();

		void LogNoNL(const char* fmt, ...);
		void Log(const char* fmt, ...);
		void LogWarn(const char* fmt, ...);
		void LogError(const char* fmt, ...);
		void PostInit();

		static fs::Version const* GetVersion(const char* hash);
		static bool FileExists(const char* name);

	private:
		fs::Installation _installation;
		Updater _updater;

		/* Window building. */
		void AddLauncherConfigurationOptions();
		void AddLaunchOptions();
		void AddRepentogonOptions();
		void AddVanillaOptions();
		void AddAdvancedOptions();

		void AddLauncherConfigurationTextField(const char* intro, 
			const char* buttonText, const char* emptyText, wxColour const& emptyColor, 
			wxBoxSizer* sizer, wxTextCtrl** result);

		/* Check Isaac version, Repentogon installation and Repentogon version. 
		 * Disable Repentogon options if Repentogon is not installed correctly.
		 * 
		 * This only checks the consistency of Repentogon's version, i.e. both
		 * ZHL and the main mod have the same version. See CheckRepentogonUpdates
		 * for the function that checks if updates are available.
		 */
		void CheckVersionsAndInstallation();

		/* Check if there is a Repentogon update available. This check is performed
		 * by comparing the "name" field of the latest release on GitHub with the 
		 * version global of ZHL / Repentogon.
		 */
		std::optional<rapidjson::Document> CheckRepentogonUpdates();

		/* Check if there is a launcher update available. This check is performed 
		 * by comparing the "name" field of the latest release on GitHub with the 
		 * version global of the launcher.
		 */
		std::optional<rapidjson::Document> CheckSelfUpdates();

		/* Check for the availability of updates. This check is performed by 
		 * comparing the "name" field of the latest release at the given url with
		 * the currentVersion.
		 */
		std::optional<rapidjson::Document> CheckUpdates(const char* url, const char* currentVersion);

		bool DoRepentogonUpdate(rapidjson::Document& document);
		bool DoSelfUpdate(rapidjson::Document& document);

		/* Function to be run in a thread in order to download the latest Repentogon 
		 * release when checking for updates. The main thread will block but the log
		 * window will still update.
		 */
		void DownloadLatestRelease();

		/* Initialize the IsaacOptions structure. */
		void InitializeOptions();
		/* Initialize GUI components from the IsaacOptions structure. */
		void InitializeGUIFromOptions();
		/* Helper to initialize the level selection field from the IsaacOptions structure. */
		void InitializeLevelSelectFromOptions();
		/* Enable/disable Repentogon options depending on the selected launch mode (in GUI). */
		void UpdateRepentogonOptionsFromLaunchMode();

		void WriteLauncherConfiguration();

		/* Event handlers. */
		void OnIsaacSelectClick(wxCommandEvent& event);
		void OnLevelSelect(wxCommandEvent& event);
		void OnLauchModeSelect(wxCommandEvent& event);
		void OnCharacterWritten(wxCommandEvent& event);
		void OnOptionSelected(wxCommandEvent& event);
		void OnSelfUpdateClick(wxCommandEvent& event);
		void ForceUpdate(wxCommandEvent& event);
		void Launch(wxCommandEvent& event);
		void Inject();

		/* Post init stuff. */
		bool CheckIsaacInstallation();
		bool CheckIsaacVersion();
		bool CheckRepentogonInstallation();

		/* Prompt the user for a place to store the configuration file.	*/
		Launcher::fs::ConfigurationFileLocation PromptConfigurationFileLocation();

		/* Prompt the user for the folder containing an Isaac installation. */
		std::string PromptIsaacInstallation();

		/* Prompt the user for a Repentogon installation. This is useful if we 
		 * don't detect a valid Repentogon installation. 
		 * 
		 * Return true if the user wants a download, false otherwise.
		 */
		bool PromptRepentogonInstallation();

		/* Download and install the latest release of Repentogon.
		 * 
		 * Parameter force indicates whether the function will download and 
		 * install the latest release even if the installation is up-to-date.
		 * 
		 * If a Repentogon installation has been detected, the function will 
		 * check for the presence of dsound.dll, asking the user if they want 
		 * that file deleted should it be found.
		 */
		void DownloadAndInstallRepentogon(bool force);

		/* Check if an update is available. 
		 * 
		 * Parameter force will cause the function to consider that an update is
		 * available even if the installation is up-to-date.
		 * 
		 * Return false if the request for up-to-dateness fails in any way.
		 * Return true if there is an update available.
		 * Return false if the request succeeds, and the installation is up-to-date
		 * and force is false.
		 * Return true if the request succeeds, and the installation is up-to-date
		 * and force is true.
		 */
		bool DoCheckRepentogonUpdates(rapidjson::Document& document, bool force);

		IsaacOptions _options;
		// wxGridBagSizer* _optionsGrid;
		wxBoxSizer* _optionsSizer;
		wxBoxSizer* _configurationSizer;
		wxBoxSizer* _advancedSizer;
		wxCheckBox* _console;
		wxCheckBox* _luaDebug;
		wxComboBox* _levelSelect;
		wxComboBox* _launchMode;
		wxTextCtrl* _luaHeapSize;
		wxStaticBox* _optionsBox;
		wxStaticBox* _configurationBox;
		wxStaticBox* _repentogonOptions;
		wxStaticBox* _advancedOptions;
		wxTextCtrl* _isaacFileText;
		wxTextCtrl* _repentogonInstallFolderText;

		std::mutex _logMutex;
		wxTextCtrl* _logWindow;
		char* _repentogonVersion;
		/* Log string used in CheckUpdates to indicate which tool is being checked. */
		std::string _currentUpdate;

		bool _hasIsaac = false;
		bool _hasRepentogon = false;

		wxDECLARE_EVENT_TABLE();
	};
}