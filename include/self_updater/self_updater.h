#pragma once

#include <Windows.h>
#include <WinBase.h>
#include <unordered_map>
#include <atomic>
#include <memory>

#include "self_updater/unique_window.h"

namespace Updater {
	/* Name of the CLI flag that causes the lock file to be ignored. */
	extern const char* ForcedArg;
	/* Name of the CLI flag that allows updating to unstable versions. */
	extern const char* UnstableArg;
	/* Name of the CLI flag for the launcher to pass its current process ID to the updater, so that the updater can verify/terminate it. */
	extern const char* LauncherProcessIdArg;
	/* URL of the release to download. */
	extern const char* ReleaseURL;
	/* Version to which we are upgrading. */
	extern const char* UpgradeVersion;

	// Represents the final state of an attempted execution.
	enum UpdateLauncherResult {
		UPDATE_ERROR,
		UPDATE_ALREADY_UP_TO_DATE,
		UPDATE_SKIPPED,
		UPDATE_SUCCESSFUL,
	};

	// Represents the current state of an ongoing update attempt.
	// Used to communicate the current state between between threads.
	enum UpdaterState {
		UPDATER_STARTUP,
		UPDATER_CHECKING_FOR_UPDATE,
		UPDATER_PROMPTING_USER,
		UPDATER_DOWNLOADING_UPDATE,
		UPDATER_INSTALLING_UPDATE,
		UPDATER_STARTING_LAUNCHER,
		UPDATER_FAILED,
	};

	// Text to be displayed on the progress bar window for each appropriate UpdaterState.
	// If a state has no text defined, the progress bar disappears during that state.
	extern const std::unordered_map<UpdaterState, const char*> UpdaterStateProgressBarLabels;

	// Current UpdaterState of the process.
	// Set by the main thread and read by the progress bar thread.
	extern std::atomic<UpdaterState> _currentState;

	// Loads the current value of `_currentState`.
	UpdaterState GetCurrentUpdaterState();

	// Stores a new value into `_currentState`.
	void SetCurrentUpdaterState(UpdaterState newState);

	// Returns true if the provided handle seems to correspond to the launcher exe.
	bool HandleMatchesLauncherExe(HANDLE handle);

	// Open a handle for the active launcher process and return it, if a process ID was provided on the command line by the launcher itself.
	// Returns INVALID_HANDLE_VALUE if not provided or no corresponding process could be found, in which case we'll assume the launcher is not running.
	// (If it is actually running, we will notice the lock in the launcher exe later and deal with it then.)
	HANDLE TryOpenLauncherProcessHandle(int argc, char** argv);

	// The updater "updates" its own exe by renaming it instead so that the new version can be written.
	// This function checks for the renamed file from a previous update and tries to delete it if found.
	// Returns false if the file could not be deleted.
	// The updater cannot continue in this case.
	bool TryDeleteOldRenamedUpdaterExe(HWND mainWindow);

	// Returns false if the launcher exe is locked, most likely meaning it is still running.
	// The updater cannot continue in this case.
	bool VerifyLauncherNotRunning(HWND mainWindow);

	// Initializes the window for the progress bar and returns the handle within a UniqueWindow wrapper.
	// Returns nullptr if the window could not be created.
	std::unique_ptr<Updater::UniqueWindow> CreateProgressBarWindow(HWND mainWindow);

	// A progress bar window is created/run on a separate thread so that it doesn't freeze during the update.
	// The handle of the progress bar window is sent back to the main thread via the promise.
	void ProgressBarThread(HWND mainWindow);

	// Starts the launcher exe.
	bool StartLauncher();

	// "Main" function that initiates the self-update process.
	UpdateLauncherResult TryUpdateLauncher(int argc, char** argv, HWND mainWindow);
}