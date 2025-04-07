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

	enum UpdaterState {
		UPDATER_STARTUP,
		UPDATER_CHECKING_FOR_UPDATE,
		UPDATER_PROMPTING_USER,
		UPDATER_DOWNLOADING_UPDATE,
		UPDATER_INSTALLING_UPDATE,
		UPDATER_STARTING_LAUNCHER,
		UPDATER_FAILED,
	};

	// Text to be displayed on the progress bar window for each appropriate state.
	// If a state has no text defined, the progress bar disappears during that state.
	extern const std::unordered_map<UpdaterState, const char*> UpdaterStateProgressBarLabels;

	// Current state of the updater process.
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

	// Initializes the window for the progress bar and returns the handle within a UniqueWindow wrapper.
	// Returns nullptr if the window could not be created.
	std::unique_ptr<Updater::UniqueWindow> CreateProgressBarWindow();

	// A progress bar window is created/run on a separate thread so that it doesn't freeze during the update.
	// The handle of the progress bar window is sent back to the main thread via the promise.
	void ProgressBarThread();

	// Starts the launcher exe, then terminates this process.
	[[noreturn]] void StartLauncher();
}