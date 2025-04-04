#include <Windows.h>
#include <WinBase.h>
#include <filesystem>
#include <Commctrl.h>
#include <thread>

#include "comm/messages.h"
#include "self_updater/self_updater.h"
#include "self_updater/logger.h"
#include "self_updater/utils.h"
#include "self_updater/launcher_update_manager.h"
#include "self_updater/unpacker.h"
#include "launcher/version.h"

// Enables windows visual styles for a nicer-looking progress bar & the marquee style.
// Incantation obtained from official docs: https://learn.microsoft.com/en-us/windows/win32/controls/cookbook-overview
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

const char* Updater::ForcedArg = "--force";
const char* Updater::UnstableArg = "--unstable";
const char* Updater::LauncherProcessIdArg = "--launcherpid";

namespace Updater {
	static const char* LauncherExePath = "./REPENTOGONLauncher.exe";
	static const char* UpdaterProcessName = "REPENTOGON Launcher Updater";

	static const std::unordered_map<UpdaterState, const char*> UpdaterStateProgressBarLabels = {
		{ UPDATER_CHECKING_FOR_UPDATE, "REPENTOGON Launcher Updater: Checking for update..." },
		{ UPDATER_DOWNLOADING_UPDATE, "REPENTOGON Launcher Updater: Downloading update..." },
		{ UPDATER_INSTALLING_UPDATE, "REPENTOGON Launcher Updater: Installing update..." },
		{ UPDATER_STARTING_LAUNCHER, "REPENTOGON Launcher Updater: Starting launcher..." },
	};

	static std::atomic<UpdaterState> _currentState = UPDATER_STARTUP;
}

HANDLE Updater::TryOpenLauncherProcessHandle(int argc, char** argv) {
	if (!Utils::HasLauncherProcessIdArg(argc, argv)) {
		// Arg not provided.
		return NULL;
	}

	char* launcherProcessIdParam = Utils::GetLauncherProcessIdArg(argc, argv);
	if (!launcherProcessIdParam) {
		Logger::Error("%s arg provded with no value!", LauncherProcessIdArg);
		return NULL;
	}

	DWORD launcherProcessId = 0;
	try {
		launcherProcessId = std::stoul(launcherProcessIdParam);
	} catch (const std::exception& ex) {
		Logger::Error("Failed to parse launcher process ID param from the command line (%s): %s", launcherProcessIdParam, ex.what());
		return NULL;
	}

	HANDLE launcherHandle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, launcherProcessId);
	if (launcherHandle == NULL) {
		// Launcher may have closed already.
		Logger::Warn("Error while trying to open process for provided process ID (%d)\n", GetLastError());
		return NULL;
	}

	// Query the name/filepath of the process and verify that it corresponds to the launcher exe.
	char buffer[MAX_PATH];
	DWORD bufferSize = MAX_PATH;
	BOOL getProcessNameSuccess = QueryFullProcessImageNameA(launcherHandle, 0, buffer, &bufferSize);
	if (!getProcessNameSuccess) {
		Logger::Error("Error while querying the name of the provided process ID (%d)\n", GetLastError());
		return NULL;
	}

	try {
		if (std::filesystem::equivalent(buffer, LauncherExePath)) {
			return launcherHandle;
		}
	} catch (const std::filesystem::filesystem_error& ex) {
		// Most likely one or the other does not exist.
		Logger::Warn("Error when checking filepath equivalence of `%s` and `%s`: %s", buffer, LauncherExePath, ex.what());
	}

	// This is not the launcher. Assume that the launcher already closed, and the pid may have been re-used.
	Logger::Warn("Provided process ID is not the launcher exe (found process %s)\n", buffer);
	CloseHandle(launcherHandle);
	return NULL;
}

HWND Updater::CreateProgressBarWindow() {
	HINSTANCE hInstance = GetModuleHandle(NULL);

	WNDCLASSEX windowClass = {};

	if (!GetClassInfoExA(hInstance, "SelfUpdaterProgressBar", &windowClass)) {
		windowClass.cbSize = sizeof(windowClass);
		windowClass.lpszClassName = "SelfUpdaterProgressBar";
		windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW);
		windowClass.lpfnWndProc = DefWindowProcA;
		windowClass.hInstance = hInstance;
		windowClass.style = windowClass.style | CS_NOCLOSE;

		if (!RegisterClassExA(&windowClass)) {
			Logger::Error("Error registering progress bar window class (%d)\n", GetLastError());
			return NULL;
		}
	}

	// Create main window
	HWND window = CreateWindowExA(0, windowClass.lpszClassName, Updater::UpdaterProcessName, WS_POPUPWINDOW | WS_CAPTION | WS_SYSMENU, 0, 0, 450, 95, NULL, NULL, windowClass.hInstance, NULL);
	if (!window) {
		Logger::Error("Error creating progress bar window (%d)\n", GetLastError());
		return NULL;
	}

	// Move window to center of screen
	RECT rect;
	if (!GetWindowRect(window, &rect)) {
		Logger::Error("Error getting rect of progress bar window (%d)\n", GetLastError());
		DestroyWindow(window);
		return NULL;
	}
	const int xpos = (GetSystemMetrics(SM_CXSCREEN) - rect.right) / 2;
	const int ypos = (GetSystemMetrics(SM_CYSCREEN) - rect.bottom) / 2;
	if (!SetWindowPos(window, 0, xpos, ypos, 0, 0, SWP_NOSIZE | SWP_NOZORDER)) {
		Logger::Error("Error setting position of progress bar window (%d)\n", GetLastError());
		DestroyWindow(window);
		return NULL;
	}

	// Add progress bar to window
	HWND progressBar = CreateWindowExA(0, PROGRESS_CLASS, "", WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 15, 15, 400, 25, window, NULL, windowClass.hInstance, NULL);
	if (progressBar == NULL) {
		Logger::Error("Error initializing progress bar (%d)\n", GetLastError());
		DestroyWindow(window);
		return NULL;
	}
	SendMessageA(progressBar, PBM_SETMARQUEE, 1, NULL);

	// Finish window init
	ShowWindow(window, SW_SHOWDEFAULT);
	if (!UpdateWindow(window)) {
		Logger::Error("Error finalizing progress bar window (%d)\n", GetLastError());
		DestroyWindow(window);
		return NULL;
	}

	return window;
}

void Updater::ProgressBarThread() {
	HWND progressBarWindow = NULL;

	UpdaterState state = UPDATER_STARTUP;

	MSG msg = {};
	BOOL bRet = 0;
	
	while (msg.message != WM_QUIT) {
		if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		} else if (_currentState != state) {
			state = _currentState;

			if (UpdaterStateProgressBarLabels.find(state) != UpdaterStateProgressBarLabels.end()) {
				if (progressBarWindow == NULL) {
					progressBarWindow = Updater::CreateProgressBarWindow();
					if (progressBarWindow == NULL) {
						return;
					}
				}
				if (!SetWindowTextA(progressBarWindow, UpdaterStateProgressBarLabels.at(state))) {
					Logger::Error("Error trying to set text of progress bar window (%d)\n", GetLastError());
					return;
				}
			} else if (progressBarWindow != NULL) {
				if (!DestroyWindow(progressBarWindow)) {
					Logger::Error("Error while attempting to destroy the progress bar window (%d)\n", GetLastError());
				}
				progressBarWindow = NULL;
			}
		}
	}

	Logger::Error("Progress bar thread ended.\n");
}

void Updater::StartLauncher() {
	STARTUPINFOA startup;
	memset(&startup, 0, sizeof(startup));

	PROCESS_INFORMATION info;
	memset(&info, 0, sizeof(info));

	char cli[] = { 0 };
	BOOL created = CreateProcessA(LauncherExePath, cli, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info);
	if (!created) {
		Logger::Error("Failed to start launcher.\n");
		ExitProcess(-1);
	} else {
		ExitProcess(0);
	}
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR cli, int) {
	Logger::Init("updater.log");

	Logger::Info("%s started with command-line args: %s\n", Updater::UpdaterProcessName, cli);

	// Parse command-line
	int argc = 0;
	char** argv = Updater::Utils::CommandLineToArgvA(cli, &argc);

	// Open a handle for the active launcher process, if provided by the launcher itself.
	HANDLE launcherHandle = Updater::TryOpenLauncherProcessHandle(argc, argv);

	// Validate that no other instance of the updater is running.
	HANDLE lockFile = NULL;

	Updater::Utils::LockUpdaterResult lockResult = Updater::Utils::LockUpdater(&lockFile);

	if (lockResult != Updater::Utils::LOCK_UPDATER_SUCCESS) {
		bool isForced = Updater::Utils::IsForced(argc, argv);

		if (!isForced) {
			if (lockResult == Updater::Utils::LOCK_UPDATER_ERR_INTERNAL) {
				MessageBoxA(NULL, "Unable to check if another instance of the updater is already running\n"
					"Check the log file (you may need to launch Isaac normaly at least once; otherwise rerun with --force)\n", Updater::UpdaterProcessName, MB_ICONERROR);
				Logger::Fatal("Error while attempting to lock the updater\n");
			} else {
				MessageBoxA(NULL, "Another instance of the updater is already running, "
					"terminate it first then restart the updater\n"
					"(If no other instance of the updater is running, rerun with --force)\n", Updater::UpdaterProcessName, MB_ICONERROR);
				Logger::Fatal("Launcher updater is already running, terminate other instances first\n");
			}

			return -1;
		} else {
			Logger::Warn("Cannot take updater lock, but ignoring (--force given)\n");
		}
	}

	Updater::Utils::ScopedHandle scopedHandle(lockFile);

	// Start up a progress bar window on a separate thread so that we can show some not-frozen UI to the user.
	std::thread progressBarThread(Updater::ProgressBarThread);

	Updater::_currentState = Updater::UPDATER_CHECKING_FOR_UPDATE;

	// Check for available updates.
	// TODO: Properly hook up logging from LauncherUpdateManager
	NopLogGUI gui;
	using lu = Updater::LauncherUpdateManager;
	lu updateManager(&gui);
	
	bool allowUnstable = Updater::Utils::AllowUnstable(argc, argv);
	if (allowUnstable) {
		Logger::Info("Fetching unstable releases is enabled.\n");
	}
	std::string version;
	std::string url;
	lu::SelfUpdateCheckResult checkResult = updateManager.CheckSelfUpdateAvailability(allowUnstable, version, url);

	switch (checkResult) {
	case lu::SELF_UPDATE_CHECK_UP_TO_DATE: {
		Logger::Info("launcher is already up-to-date.\n");
		Updater::StartLauncher();
		return 0;
	}

	case lu::SELF_UPDATE_CHECK_ERR_GENERIC: {
		MessageBoxA(NULL, "An error was encountered while checking for the availability of updates. Check the log file for more details.\n", Updater::UpdaterProcessName, MB_ICONERROR);
		Logger::Fatal("Error while checking for updates.\n");
		return -1;
	}

	case lu::SELF_UPDATE_CHECK_UPDATE_AVAILABLE:
		Logger::Info("Found available update version: %s (from %s)\n", version.c_str(), url.c_str());
		break;

	default: {
		Logger::Fatal("Recieved unexpected error code %d while checking for availability of self updates. Please report this as a bug.\n", checkResult);
		return -1;
	}
	}

	Updater::_currentState = Updater::UPDATER_PROMPTING_USER;

	// An appropriate update is available. Prompt the user about it.
	const std::string updatePrompt = (std::ostringstream() << "An update is available for the launcher.\n" <<
		"It will update from version " << Launcher::version << " to version " << version << ".\n\n" <<
		"Do you want to update now?").str();

	int updatePromptResult = MessageBoxA(NULL, updatePrompt.c_str(), Updater::UpdaterProcessName, MB_ICONINFORMATION | MB_YESNO);
	if (updatePromptResult != IDOK && updatePromptResult != IDYES) {
		Logger::Info("Launcher updated rejected by user\n");
		return 0;
	}

	Updater::_currentState = Updater::UPDATER_DOWNLOADING_UPDATE;

	// If we opened a launcher process handle, terminate it now.
	if (launcherHandle != NULL) {
		Logger::Info("Terminating launcher...\n");
		if (TerminateProcess(launcherHandle, 0)) {
			WaitForSingleObject(launcherHandle, INFINITE);
		} else {
			// We continue on after this error since we'll notice if the launcher exe is locked and prompt the user about it then.
			Logger::Error("Failed to terminate launcher process (%d)\n", GetLastError());
		}
		CloseHandle(launcherHandle);
	}

	Logger::Info("Starting update download from %s\n", url.c_str());

	if (!updateManager.DownloadAndExtractUpdate(url.c_str())) {
		Updater::_currentState = Updater::UPDATER_FAILED;
		MessageBoxA(NULL, "Failed to download the update for the launcher. Check the log file for more details.\n", Updater::UpdaterProcessName, MB_ICONERROR);
		Logger::Fatal("Error trying to download update from URL: %s\n", url.c_str());
		return -1;
	}

	Updater::_currentState = Updater::UPDATER_INSTALLING_UPDATE;

	// Check if the updater exe is still locked. If so, prompt the user about it.
	// The exe file can remain locked very briefly after terminating the launcher, so we check this after the download.
	if (std::filesystem::exists(Updater::LauncherExePath)) {
		int attempts = 0;
		while (true) {
			attempts++;
			HANDLE fh = CreateFileA(Updater::LauncherExePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
			if (fh == INVALID_HANDLE_VALUE) {
				if (attempts == 1) {
					Logger::Error("The launcher exe is currently locked!\n");
				}
				if (attempts <= 5) {
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					continue;
				}
				int response = MessageBoxA(NULL, "Cannot update the REPENTOGON Launcher, as it is currently running.\nPlease close it and try again.\n", Updater::UpdaterProcessName, MB_ICONINFORMATION | MB_RETRYCANCEL);
				if (response == IDRETRY || response == IDOK || response == IDTRYAGAIN) {
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}
				Logger::Fatal("The launcher is currently running, and the user chose to cancel.\n");
				return -1;
			} else {
				CloseHandle(fh);
				break;
			}
		}
	}

	if (!Unpacker::ExtractArchive(Comm::UnpackedArchiveName)) {
		Updater::_currentState = Updater::UPDATER_FAILED;
		MessageBoxA(NULL, "Unable to unpack the update, check log file\n", Updater::UpdaterProcessName, MB_ICONERROR);
		Logger::Fatal("Error while extracting archive\n");
		return -1;
	}

	if (!DeleteFileA(Comm::UnpackedArchiveName)) {
		Updater::_currentState = Updater::UPDATER_FAILED;
		MessageBoxA(NULL, "Unable to delete the archive containing the update.\n"
			"Delete the archive (launcher_update.bin) then you can start the launcher\n", Updater::UpdaterProcessName, MB_ICONERROR);
		Logger::Fatal("Error while deleting archive (%d)\n", GetLastError());
		return -1;
	}

	Updater::_currentState = Updater::UPDATER_STARTING_LAUNCHER;

	Logger::Info("Update successful. Starting launcher...\n");
	Updater::StartLauncher();

	return 0;
}