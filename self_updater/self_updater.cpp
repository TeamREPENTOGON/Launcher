#include <Windows.h>
#include <WinBase.h>
#include <filesystem>
#include <Commctrl.h>
#include <thread>
#include <memory>

#include "comm/messages.h"
#include "shared/github_executor.h"
#include "shared/logger.h"
#include "self_updater/self_updater.h"
#include "self_updater/utils.h"
#include "self_updater/launcher_update_manager.h"
#include "self_updater/unique_window.h"
#include "self_updater/unpacker.h"
#include "launcher/version.h"

// Enables windows visual styles for a nicer-looking progress bar & the marquee style.
// Incantation obtained from official docs: https://learn.microsoft.com/en-us/windows/win32/controls/cookbook-overview
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

const char* Updater::ForcedArg = "--force";
const char* Updater::UnstableArg = "--unstable";
const char* Updater::LauncherProcessIdArg = "--launcherpid";
const char* Updater::ReleaseURL = "--url";
const char* Updater::UpgradeVersion = "--version";

namespace Updater {
	static const char* LauncherExePath = "./REPENTOGONLauncher.exe";
	static const char* UpdaterProcessName = "REPENTOGON Launcher Updater";

	// During update installation, the existing updater exe is renamed to make room for the new one.
	// We can't delete our own exe (since it is obviously running) but renaming it is allowed.
	// The old renamed exe will be deleted the next time the updater runs.
	static const char* UpdaterExeFilename = "REPENTOGONLauncherUpdater.exe";
	static const char* RenamedUpdaterExeFilename = "REPENTOGONLauncherUpdater.exe.bak";

	const std::unordered_map<UpdaterState, const char*> UpdaterStateProgressBarLabels = {
		{ UPDATER_CHECKING_FOR_UPDATE, "REPENTOGON Launcher Updater: Checking for update..." },
		{ UPDATER_DOWNLOADING_UPDATE, "REPENTOGON Launcher Updater: Downloading update..." },
		{ UPDATER_INSTALLING_UPDATE, "REPENTOGON Launcher Updater: Installing update..." },
		{ UPDATER_STARTING_LAUNCHER, "REPENTOGON Launcher Updater: Starting launcher..." },
	};

	std::atomic<UpdaterState> _currentState = UPDATER_STARTUP;
}

Updater::UpdaterState Updater::GetCurrentUpdaterState() {
	return _currentState.load(std::memory_order_acquire);
}

void Updater::SetCurrentUpdaterState(Updater::UpdaterState newState) {
	_currentState.store(newState, std::memory_order_release);
}

bool Updater::HandleMatchesLauncherExe(HANDLE handle) {
	// Query the name/filepath of the process and verify that it corresponds to the launcher exe.
	char buffer[MAX_PATH];
	DWORD bufferSize = MAX_PATH;
	BOOL getProcessNameSuccess = QueryFullProcessImageNameA(handle, 0, buffer, &bufferSize);
	if (!getProcessNameSuccess) {
		Logger::Error("Error while querying the name of the provided process ID (%d)\n", GetLastError());
		return false;
	}

	Logger::Info("Comparing path of provided process ID (%s) to the launcher exe (%s)\n", buffer, LauncherExePath);

	try {
		return std::filesystem::equivalent(buffer, LauncherExePath);
	}
	catch (const std::filesystem::filesystem_error& ex) {
		// Most likely one or the other does not exist.
		Logger::Warn("Error when checking filepath equivalence of `%s` and `%s`: %s", buffer, LauncherExePath, ex.what());
	}
	return false;
}

HANDLE Updater::TryOpenLauncherProcessHandle(int argc, char** argv) {
	char* launcherProcessIdParam = Utils::GetLauncherProcessIdArg(argc, argv);
	if (!launcherProcessIdParam) {
		return INVALID_HANDLE_VALUE;
	}

	DWORD launcherProcessId = NULL;
	try {
		launcherProcessId = std::stoul(launcherProcessIdParam);
	} catch (const std::exception& ex) {
		Logger::Error("Failed to parse launcher process ID param from the command line (%s): %s", launcherProcessIdParam, ex.what());
		return INVALID_HANDLE_VALUE;
	}

	HANDLE launcherHandle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, launcherProcessId);
	if (launcherHandle == NULL) {
		// Launcher may have closed already.
		Logger::Warn("Error while trying to open process for provided process ID (%d)\n", GetLastError());
		return INVALID_HANDLE_VALUE;
	}

	if (!HandleMatchesLauncherExe(launcherHandle)) {
		// This is not the launcher. Assume that the launcher already closed, and the pid may have been re-used.
		Logger::Warn("Provided process ID is not the launcher exe. Ignoring it.\n");
		CloseHandle(launcherHandle);
		return INVALID_HANDLE_VALUE;
	}

	return launcherHandle;
}

bool Updater::TryDeleteOldRenamedUpdaterExe() {
	int attempts = 0;
	while (std::filesystem::exists(RenamedUpdaterExeFilename) && !DeleteFileA(RenamedUpdaterExeFilename)) {
		attempts++;
		if (attempts == 1) {
			Logger::Error("Error trying to delete %s from previous update (%d)\n", RenamedUpdaterExeFilename, GetLastError());
		}
		if (attempts <= 5) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}
		SetCurrentUpdaterState(UPDATER_PROMPTING_USER);
		int response = MessageBoxA(NULL, "Failed to delete REPENTOGONLauncherUpdater.exe.bak file from prior update.\n"
			"The update was most likely successful, but please report this as a bug.\n"
			"Select \"cancel\" to allow the Launcher to start.\n", UpdaterProcessName, MB_ICONINFORMATION | MB_RETRYCANCEL);
		if (response == IDRETRY || response == IDOK || response == IDTRYAGAIN) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}
		Logger::Error("Failed to delete %s from previous update, and the user chose to cancel.\n", RenamedUpdaterExeFilename);
		return false;
	}

	return true;
}

bool Updater::VerifyLauncherNotRunning() {
	Utils::ScopedHandle fh(INVALID_HANDLE_VALUE);

	int attempts = 0;
	while (std::filesystem::exists(LauncherExePath) && (fh = Utils::ScopedHandle(CreateFileA(LauncherExePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL))) == INVALID_HANDLE_VALUE) {
		attempts++;
		if (attempts == 1) {
			Logger::Error("The launcher exe is currently locked!\n");
		}
		if (attempts <= 5) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}

		/* Cannot have a parent window here as we may be in the process of
		 * destroying the launcher.
		 */
		int response = MessageBoxA(NULL, "Cannot update the REPENTOGON Launcher, as it is currently running.\nPlease close it and try again.\n", UpdaterProcessName, MB_ICONINFORMATION | MB_RETRYCANCEL);
		if (response == IDRETRY || response == IDOK || response == IDTRYAGAIN) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			continue;
		}
		Logger::Error("The launcher is currently running, and the user chose to cancel.\n");
		return false;
	}

	return true;
}

std::unique_ptr<Updater::UniqueWindow> Updater::CreateProgressBarWindow() {
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
			return nullptr;
		}
	}

	// Create main window
	auto window = std::make_unique<UniqueWindow>(nullptr, windowClass.lpszClassName,
		Updater::UpdaterProcessName, WS_POPUPWINDOW | WS_CAPTION | WS_SYSMENU, 0,
		0, 450, 95, windowClass.hInstance);
	HWND windowHandle = window->GetHandle();
	if (windowHandle == NULL) {
		Logger::Error("Error creating progress bar window (%d)\n", GetLastError());
		return nullptr;
	}

	// Move window to center of screen
	RECT rect;
	if (!GetWindowRect(windowHandle, &rect)) {
		Logger::Error("Error getting rect of progress bar window (%d)\n", GetLastError());
		return nullptr;
	}
	const int xpos = (GetSystemMetrics(SM_CXSCREEN) - rect.right) / 2;
	const int ypos = (GetSystemMetrics(SM_CYSCREEN) - rect.bottom) / 2;
	if (!SetWindowPos(windowHandle, 0, xpos, ypos, 0, 0, SWP_NOSIZE | SWP_NOZORDER)) {
		Logger::Error("Error setting position of progress bar window (%d)\n", GetLastError());
		return nullptr;
	}

	// Add progress bar to window
	HWND progressBar = CreateWindowExA(0, PROGRESS_CLASS, "", WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 15, 15, 400, 25, windowHandle, NULL, windowClass.hInstance, NULL);
	if (progressBar == NULL) {
		Logger::Error("Error initializing progress bar (%d)\n", GetLastError());
		return nullptr;
	}
	SendMessageA(progressBar, PBM_SETMARQUEE, 1, NULL);

	// Finish window init
	ShowWindow(windowHandle, SW_SHOWDEFAULT);
	if (!UpdateWindow(windowHandle)) {
		Logger::Error("Error finalizing progress bar window (%d)\n", GetLastError());
		return nullptr;
	}

	return window;
}

void Updater::ProgressBarThread() {
	std::unique_ptr<Updater::UniqueWindow> progressBarWindow = nullptr;

	UpdaterState state = UPDATER_STARTUP;

	MSG msg = {};

	while (true) {
		while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
			if (msg.message == WM_QUIT) {
				break;
			}
		}

		const UpdaterState currentState = GetCurrentUpdaterState();
		if (currentState != state) {
			state = currentState;

			if (UpdaterStateProgressBarLabels.find(state) != UpdaterStateProgressBarLabels.end()) {
				if (!progressBarWindow) {
					progressBarWindow = Updater::CreateProgressBarWindow();
					if (!progressBarWindow) {
						Logger::Error("Failed to create progress bar window\n");
						break;
					}
				}
				if (!SetWindowTextA(progressBarWindow->GetHandle(), UpdaterStateProgressBarLabels.at(state))) {
					Logger::Error("Error trying to set text of progress bar window (%d)\n", GetLastError());
					break;
				}
			} else if (progressBarWindow) {
				progressBarWindow = nullptr;
			}
		}
	}

	Logger::Error("Progress bar thread ended.\n");
}

void Updater::StartLauncher() {
	Updater::SetCurrentUpdaterState(Updater::UPDATER_STARTING_LAUNCHER);
	Logger::Info("Starting launcher...\n");

	STARTUPINFOA startup;
	memset(&startup, 0, sizeof(startup));

	PROCESS_INFORMATION info;
	memset(&info, 0, sizeof(info));

	char cli[] = { 0 };
	BOOL created = CreateProcessA(LauncherExePath, cli, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info);
	if (!created) {
		Logger::Error("Failed to start launcher.\n");
		ExitProcess(1);
	} else {
		ExitProcess(0);
	}
}

Updater::UpdateLauncherResult Updater::TryUpdateLauncher(int argc, char** argv) {
	// Open a handle for the active launcher process, if provided by the launcher itself.
	HANDLE launcherHandle = TryOpenLauncherProcessHandle(argc, argv);

	// Validate that no other instance of the updater is running.
	HANDLE lockFile = NULL;

	Utils::LockUpdaterResult lockResult = Utils::LockUpdater(&lockFile);

	if (lockResult != Utils::LOCK_UPDATER_SUCCESS) {
		bool isForced = Utils::IsForced(argc, argv);

		if (!isForced) {
			if (lockResult == Utils::LOCK_UPDATER_ERR_INTERNAL) {
				MessageBoxA(NULL, "Unable to check if another instance of the updater is already running\n"
					"Check the log file (you may need to launch Isaac normaly at least once; otherwise rerun with --force)\n",
					UpdaterProcessName, MB_ICONERROR);
				Logger::Error("Error while attempting to lock the updater\n");
			}
			else {
				MessageBoxA(NULL, "Another instance of the updater is already running, "
					"terminate it first then restart the updater\n"
					"(If no other instance of the updater is running, rerun with --force)\n", UpdaterProcessName, MB_ICONERROR);
				Logger::Error("Launcher updater is already running, terminate other instances first\n");
			}

			return UPDATE_ERROR;
		}
		else {
			Logger::Warn("Cannot take updater lock, but ignoring (--force given)\n");
		}
	}

	Updater::Utils::ScopedHandle scopedHandle(lockFile);

	SetCurrentUpdaterState(UPDATER_CHECKING_FOR_UPDATE);

	// Check for available updates.
	// TODO: Properly hook up logging from LauncherUpdateManager
	NopLogGUI gui;
	using lu = LauncherUpdateManager;
	lu updateManager(&gui);

	bool allowUnstable = Utils::AllowUnstable(argc, argv);
	if (allowUnstable) {
		Logger::Info("Fetching unstable releases is enabled.\n");
	}

	const char* versionGiven = Utils::GetUpdateVersion(argc, argv);
	const char* urlGiven = Utils::GetUpdateURL(argc, argv);

	std::string version;
	std::string url;

	if (urlGiven) {
		url = urlGiven;
	}

	if (versionGiven) {
		version = versionGiven;
	}

	if (!urlGiven) {
		lu::SelfUpdateCheckResult checkResult = updateManager.CheckSelfUpdateAvailability(allowUnstable, version, url);

		switch (checkResult) {
		case lu::SELF_UPDATE_CHECK_UP_TO_DATE:
			Logger::Info("launcher is already up-to-date.\n");
			return UPDATE_ALREADY_UP_TO_DATE;

		case lu::SELF_UPDATE_CHECK_ERR_GENERIC:
			SetCurrentUpdaterState(UPDATER_FAILED);
			MessageBoxA(NULL, "An error was encountered while checking for the availability of updates. Check the log file for more details.\n", UpdaterProcessName, MB_ICONERROR);
			Logger::Error("Error while checking for updates.\n");
			return UPDATE_ERROR;

		case lu::SELF_UPDATE_CHECK_UPDATE_AVAILABLE:
			Logger::Info("Found available update version: %s (from %s)\n", version.c_str(), url.c_str());
			break;

		default:
			SetCurrentUpdaterState(UPDATER_FAILED);
			MessageBoxA(NULL, "An unknown error was encountered while checking for the availability of updates. Check the log file for more details.\n", UpdaterProcessName, MB_ICONERROR);
			Logger::Error("Recieved unexpected error code %d while checking for availability of self updates. Please report this as a bug.\n", checkResult);
			return UPDATE_ERROR;
		}
	}

	// Check for a renamed updater exe from a previous update and try to delete it if found.
	// Since we have the lockfile to prevent multiple instances of the updater running at the same time,
	// we shouldn't ever fail to delete it, but you never know...
	if (!TryDeleteOldRenamedUpdaterExe()) {
		return UPDATE_ERROR;
	}

	SetCurrentUpdaterState(UPDATER_PROMPTING_USER);

	if (!urlGiven) {
		// An appropriate update is available. Prompt the user about it.
		const std::string updatePrompt = (std::ostringstream() << "An update is available for the launcher.\n" <<
			"It will update from version " << Launcher::version << " to version " << version << ".\n\n" <<
			"Do you want to update now?").str();

		int updatePromptResult = MessageBoxA(NULL, updatePrompt.c_str(), UpdaterProcessName, MB_ICONINFORMATION | MB_YESNO);
		if (updatePromptResult != IDOK && updatePromptResult != IDYES) {
			Logger::Info("Launcher updated rejected by user\n");
			return UPDATE_SKIPPED;
		}
	}

	SetCurrentUpdaterState(UPDATER_DOWNLOADING_UPDATE);

	// If we opened a launcher process handle, terminate it now.
	if (launcherHandle != INVALID_HANDLE_VALUE) {
		Logger::Info("Terminating launcher...\n");
		if (TerminateProcess(launcherHandle, 0)) {
			WaitForSingleObject(launcherHandle, INFINITE);
		}
		else {
			// We continue on after this error since we'll notice if the launcher exe is locked and prompt the user about it then.
			Logger::Error("Failed to terminate launcher process (%d)\n", GetLastError());
		}
		CloseHandle(launcherHandle);
	}

	Logger::Info("Starting update download from %s\n", url.c_str());

	if (!updateManager.DownloadAndExtractUpdate(url.c_str())) {
		SetCurrentUpdaterState(UPDATER_FAILED);
		MessageBoxA(NULL, "Failed to download the update for the launcher. Check the log file for more details.\n", UpdaterProcessName, MB_ICONERROR);
		Logger::Error("Error trying to download update from URL: %s\n", url.c_str());
		return UPDATE_ERROR;
	}

	SetCurrentUpdaterState(UPDATER_INSTALLING_UPDATE);

	// Check if the launcher exe is still locked. If so, prompt the user about it.
	// The exe file can remain locked very briefly after terminating the launcher, so we check this after the download.
	if (!VerifyLauncherNotRunning()) {
		return UPDATE_ERROR;
	}

	// The updater cannot directly overwrite its own currently-running exe with the new one from the update.
	// To get around this, we do the following:
	//   1. Rename the existing exe as `exe.bak`. Windows lets us rename a file thats in use, but the file remains locked until this program ends.
	//   2. Make a copy of the existing exe under the original name, just in case we encounter an error during unpacking and can't write the new exe.
	// So the result of this is that the running exe has been renamed as `exe.bak` (and is still locked by windows),
	// AND we have a copy under the original name (which is not locked and can be overwritten).
	if (std::filesystem::exists(UpdaterExeFilename)) {
		std::filesystem::rename(UpdaterExeFilename, RenamedUpdaterExeFilename);
		std::filesystem::copy(RenamedUpdaterExeFilename, UpdaterExeFilename);
	}

	if (!Unpacker::ExtractArchive(Comm::UnpackedArchiveName)) {
		SetCurrentUpdaterState(UPDATER_FAILED);
		MessageBoxA(NULL, "Unable to unpack the update, check log file\n", UpdaterProcessName, MB_ICONERROR);
		Logger::Error("Error while extracting archive\n");
		return UPDATE_ERROR;
	}

	if (!DeleteFileA(Comm::UnpackedArchiveName)) {
		SetCurrentUpdaterState(UPDATER_FAILED);
		MessageBoxA(NULL, "Unable to delete the archive containing the update.\n"
			"Delete the archive (launcher_update.bin) then you can start the launcher\n", UpdaterProcessName, MB_ICONERROR);
		Logger::Error("Error while deleting archive (%d)\n", GetLastError());
		return UPDATE_ERROR;
	}

	Logger::Info("Update successful!\n");
	return UPDATE_SUCCESSFUL;
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR cli, int) {
	Logger::Init("updater.log", true);
	Logger::Info("%s started with command-line args: %s\n", Updater::UpdaterProcessName, cli);

	int argc = 0;
	char** argv = Updater::Utils::CommandLineToArgvA(cli, &argc);

	sGithubExecutor->Start();

	// Start up a progress bar window on a separate thread so that we can show some not-frozen UI to the user.
	std::thread progressBarThread(Updater::ProgressBarThread);
	progressBarThread.detach();

	Updater::UpdateLauncherResult result = Updater::TryUpdateLauncher(argc, argv);

	int res = -1;

	switch (result) {
	case Updater::UPDATE_ALREADY_UP_TO_DATE:
	case Updater::UPDATE_SKIPPED:
		res = 0;
		break;

	case Updater::UPDATE_SUCCESSFUL:
		Updater::StartLauncher();
		res = 0;
		break;

	case Updater::UPDATE_ERROR:
		res = -1;
		break;

	default:
		Logger::Error("Unexpected update result value: %d", result);
		res = -1;
		break;
	}

	sGithubExecutor->Stop();
	return res;
}