#pragma once

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
	#include <wx/wx.h>
	#include <wx/gbsizer.h>
#endif

#include "launcher/launcher.h"
#include "rapidjson/document.h"

namespace IsaacLauncher {
	static constexpr const char* version = "alpha";

	struct Version {
		const char* hash;
		const char* version;
		bool valid;
	};

	extern Version const knownVersions[];

	enum Windows {
		WINDOW_COMBOBOX_LEVEL,
		WINDOW_COMBOBOX_LAUNCH_MODE,
		WINDOW_CHECKBOX_REPENTOGON_CONSOLE,
		WINDOW_CHECKBOX_REPENTOGON_UPDATES,
		WINDOW_CHECKBOX_VANILLA_LUADEBUG,
		WINDOW_TEXT_VANILLA_LUAHEAPSIZE,
		WINDOW_BUTTON_LAUNCH_BUTTON
	};

	class App : public wxApp {
	public:
		bool OnInit() override;
	};

	class MainFrame : public wxFrame {
	public:
		MainFrame();

		void Log(const char* fmt, ...);
		void LogWarn(const char* fmt, ...);
		void LogError(const char* fmt, ...);
		void PostInit();

		static Version const* GetVersion(const char* hash);
		static bool FileExists(const char* name);

	private:
		/* Window building. */
		void AddLaunchOptions();
		void AddRepentogonOptions();
		void AddVanillaOptions();

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
		bool CheckRepentogonUpdates();

		/* Check if there is a launcher update available. This check is performed 
		 * by comparing the "name" field of the latest release on GitHub with the 
		 * version global of the launcher.
		 */
		bool CheckSelfUpdates();

		/* Check for the availability of updates. This check is performed by 
		 * comparing the "name" field of the latest release at the given url with
		 * the currentVersion.
		 */
		bool CheckUpdates(const char* url, const char* currentVersion);

		void DoRepentogonUpdate(const char* url, const char* currentVersion, rapidjson::Document& document);
		void DoSelfUpdate(const char* url, const char* currentVersion, rapidjson::Document& document);

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
		void OnLevelSelect(wxCommandEvent& event);
		void OnLauchModeSelect(wxCommandEvent& event);
		void OnCharacterWritten(wxCommandEvent& event);
		void OnOptionSelected(wxCommandEvent& event);
		void Launch(wxCommandEvent& event);
		void Inject();

		/* Post init stuff. */
		bool CheckIsaacVersion();
		bool CheckRepentogonInstallation();
		bool CheckRepentogonVersion();

		IsaacOptions _options;
		wxGridBagSizer* _grid;
		wxCheckBox* _console;
		wxCheckBox* _luaDebug;
		wxComboBox* _levelSelect;
		wxComboBox* _launchMode;
		wxTextCtrl* _luaHeapSize;
		wxTextCtrl* _logWindow;
		char* _repentogonVersion;
		/* Log string used in CheckUpdates to indicate which tool is being checked. */
		std::string _currentUpdate;

		bool _enabledRepentogon;

		wxDECLARE_EVENT_TABLE();
	};
}