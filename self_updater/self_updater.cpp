#include <Windows.h>
#include <WinBase.h>
#include <filesystem>
#include <Commctrl.h>
#include <thread>
#include <memory>
#include <fstream>
#include <set>

#include "shared/github_executor.h"
#include "shared/logger.h"
#include "self_updater/self_updater.h"
#include "self_updater/utils.h"
#include "self_updater/launcher_update_manager.h"
#include "self_updater/unique_window.h"
#include "launcher/version.h"
#include "zip.h"

// Enables windows visual styles for a nicer-looking progress bar & the marquee style.
// Incantation obtained from official docs: https://learn.microsoft.com/en-us/windows/win32/controls/cookbook-overview
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

const char* Updater::ForcedArg = "--force";
const char* Updater::UnstableArg = "--unstable";
const char* Updater::LauncherProcessIdArg = "--launcherpid";
const char* Updater::ReleaseURL = "--url";
const char* Updater::UpgradeVersion = "--version";

namespace Updater {
	static const char* LauncherBinFilename = "launcher-data.bin";
	static const char* LauncherDllPath = "./launcher-data/REPENTOGONLauncherApp.dll";
	static const char* UpdaterProcessName = "REPENTOGON Launcher Updater";

	// During update installation, the existing updater exe is renamed to make room for the new one.
	// We can't delete our own exe (since it is obviously running) but renaming it is allowed.
	// The old renamed exe will be deleted the next time the updater runs.
	static const char* UpdaterExeFilename = "REPENTOGONLauncher.exe";
	static const char* RenamedUpdaterExeFilename = "REPENTOGONLauncher.exe.bak";

	// Files related to legacy versions that should be deleted if found.
	static const std::set<std::string> LegacyCleanupFiles = {
		"REPENTOGONLauncherUpdater.exe",
		"REPENTOGONLauncherUpdater.exe.bak",
	};

	static const char* LauncherDataFolder = "launcher-data";
	static const char* LauncherDataBackupFolder = "launcher-data.old";
	static const char* UnfinishedUpdateMarker = "launcher-data/unfinished.it";

	const char* CheckSelfUpdateLauncherArg = "--check-self-update";

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

int Updater::ShowMessageBox(HWND window, const char* text, UINT flags) {
	if (window) {
		SetForegroundWindow(window);
	}
	return MessageBoxA(window, text, UpdaterProcessName, flags);
}

bool Updater::HandleMatchesLauncherExe(HANDLE handle) {
	Logger::Info("HandleMatchesLauncherExe started.\n");

	// Query the name/filepath of the process and verify that it corresponds to the launcher exe.
	char buffer[MAX_PATH];
	DWORD bufferSize = MAX_PATH;
	BOOL getProcessNameSuccess = QueryFullProcessImageNameA(handle, 0, buffer, &bufferSize);
	if (!getProcessNameSuccess) {
		Logger::Error("Error while querying the name of the provided process ID (%d)\n", GetLastError());
		return false;
	}

	Logger::Info("Comparing path of provided process ID (%s) to the launcher exe (%s)\n", buffer, UpdaterExeFilename);

	try {
		return std::filesystem::equivalent(buffer, UpdaterExeFilename);
	}
	catch (const std::filesystem::filesystem_error& err) {
		// Most likely one or the other does not exist.
		Logger::Warn("Error when checking filepath equivalence of `%s` and `%s`: %s", buffer, UpdaterExeFilename, err.what());
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

	DWORD exit_code{};
	if (!GetExitCodeProcess(launcherHandle, &exit_code)) {
		Logger::Warn("Error while trying to check for a launcher exit code (%d)\n", GetLastError());
		return INVALID_HANDLE_VALUE;
	}

	if (exit_code != STILL_ACTIVE) {
		Logger::Warn("Pre-existing Launcher process has already been terminated (%d)\n", exit_code);
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

bool Updater::TryDeleteOldRenamedUpdaterExe(HWND mainWindow) {
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
		int response = ShowMessageBox(mainWindow, "Failed to delete REPENTOGONLauncher.exe.bak file from prior update.\n"
			"The update was most likely successful, but please report this as a bug.\n"
			"Select \"cancel\" to allow the Launcher to start.\n", MB_ICONINFORMATION | MB_RETRYCANCEL);
		if (response == IDRETRY || response == IDOK || response == IDTRYAGAIN) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}
		Logger::Error("Failed to delete %s from previous update, and the user chose to cancel.\n", RenamedUpdaterExeFilename);
		return false;
	}

	return true;
}

bool Updater::VerifyLauncherNotRunning(HWND mainWindow) {
	int attempts = 0;
	while (Utils::FileLocked(LauncherDllPath)) {
		attempts++;
		if (attempts == 1) {
			Logger::Error("The launcher exe is currently locked!\n");
		}
		if (attempts <= 5) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}

		int response = ShowMessageBox(mainWindow, "Cannot update the REPENTOGON Launcher, as it is currently running.\nPlease close it and try again.\n", MB_ICONINFORMATION | MB_RETRYCANCEL);
		if (response == IDRETRY || response == IDOK || response == IDTRYAGAIN) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			continue;
		}
		Logger::Error("The launcher is currently running, and the user chose to cancel.\n");
		return false;
	}

	return true;
}

std::unique_ptr<Updater::UniqueWindow> Updater::CreateProgressBarWindow(POINT windowPos) {
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
		Updater::UpdaterProcessName, WS_POPUPWINDOW | WS_CAPTION | WS_SYSMENU,
		windowPos.x - 225, windowPos.y - 30, 450, 95, windowClass.hInstance);
	HWND windowHandle = window->GetHandle();
	if (windowHandle == NULL) {
		Logger::Error("Error creating progress bar window (%d)\n", GetLastError());
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

void Updater::ProgressBarThread(POINT windowPos) {
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
					progressBarWindow = Updater::CreateProgressBarWindow(windowPos);
					if (!progressBarWindow) {
						Logger::Error("Failed to create progress bar window\n");
						break;
					}
				}
				SetForegroundWindow(progressBarWindow->GetHandle());
				ShowWindowAsync(progressBarWindow->GetHandle(), SW_SHOWNORMAL);
				if (!SetWindowTextA(progressBarWindow->GetHandle(), UpdaterStateProgressBarLabels.at(state))) {
					Logger::Error("Error trying to set text of progress bar window (%d)\n", GetLastError());
					break;
				}
			} else if (progressBarWindow) {
				ShowWindowAsync(progressBarWindow->GetHandle(), SW_HIDE);
			}
		}
	}

	Logger::Error("Progress bar thread ended.\n");
}

Updater::UpdateLauncherResult Updater::TryUpdateLauncher(int argc, char** argv, HWND mainWindow) {
	Logger::Info("TryUpdateLauncher started.\n");

	// Validate that no other instance of the updater is running.
	HANDLE lockFile = NULL;

	Utils::LockUpdaterResult lockResult = Utils::LockUpdater(&lockFile);

	if (lockResult != Utils::LOCK_UPDATER_SUCCESS) {
		bool isForced = Utils::IsForced(argc, argv);

		if (!isForced) {
			if (lockResult == Utils::LOCK_UPDATER_ERR_INTERNAL) {
				ShowMessageBox(mainWindow, "Unable to check if another instance of the updater is already running\n"
					"Check the log file (you may need to launch Isaac normaly at least once; otherwise rerun with --force)\n",
					MB_ICONERROR);
				Logger::Error("Error while attempting to lock the updater\n");
			}
			else {
				ShowMessageBox(mainWindow, "Another instance of the updater is already running, "
					"terminate it first then restart the updater\n"
					"(If no other instance of the updater is running, rerun with --force)\n", MB_ICONERROR);
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

	// If the version backup folder exists, and an unfinished/broken update is detected, attempt to roll back.
	if (std::filesystem::exists(LauncherDataBackupFolder) && (std::filesystem::exists(UnfinishedUpdateMarker) || !std::filesystem::exists(LauncherDllPath))) {
		Logger::Error("Detected failed update!\n");
		if (std::filesystem::exists(LauncherDataBackupFolder)) {
			Logger::Info("Attempting to restore prior installation...\n");
			std::filesystem::remove_all(LauncherDataFolder);
			std::filesystem::rename(LauncherDataBackupFolder, LauncherDataFolder);
			Logger::Info("Successfully restored previous installation!\n");
		} else {
			Logger::Error("No prior installation exists to restore!\n");
		}
	}

	// If no installation exists, but we have our bin file, attempt to extract it.
	if (!std::filesystem::exists(LauncherDllPath)) {
		if (!std::filesystem::exists(LauncherBinFilename)) {
			Logger::Warn("No installation detected, but `%s` does not exist. Will attempt to force update...\n", LauncherBinFilename);
		} else {
			if (!std::filesystem::exists(LauncherDataFolder)) {
				std::filesystem::create_directories(LauncherDataFolder);
			} else if (!std::filesystem::is_directory(LauncherDataFolder)) {
				// Dude???
				Logger::Error("`%s` is not a directory! Deleting it...\n", LauncherDataFolder);
				std::filesystem::remove(LauncherDataFolder);
				std::filesystem::create_directories(LauncherDataFolder);
			}
			Logger::Info("Extracting %s...\n", LauncherBinFilename);
			std::ofstream((const char*)UnfinishedUpdateMarker);  // Creates an empty file.
			if (!Zip::ExtractAllToFolder(LauncherBinFilename, LauncherDataFolder)) {
				Logger::Info("Extraction of `%s` failed (%d) : %s\n", LauncherBinFilename);
				std::filesystem::remove_all(LauncherDataFolder);
			} else {
				std::filesystem::remove(UnfinishedUpdateMarker);
			}
		}
	}

	// Check for steam_appid.txt
	if (!std::filesystem::exists("steam_appid.txt")) {
		Logger::Warn("steam_appid.txt is missing. Generating one...\n");
		std::ofstream outFile("steam_appid.txt");
		if (outFile) {
			outFile << "250900";
			outFile.close();
		} else {
			Logger::Error("Failed to generate steam_appid.txt!\n");
		}
	}

	// Check for available updates.
	using lu = LauncherUpdateManager;
	lu updateManager;

	const bool allowUnstable = Utils::AllowUnstable(argc, argv);
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

	const bool launcherMissing = !std::filesystem::exists(LauncherDllPath);
	const bool skipConfirmation = urlGiven || launcherMissing;

	if (!urlGiven) {
		lu::SelfUpdateCheckResult checkResult = updateManager.CheckSelfUpdateAvailability(allowUnstable, version, url);

		switch (checkResult) {
		case lu::SELF_UPDATE_CHECK_UP_TO_DATE:
			if (launcherMissing) {
				Logger::Info("Launcher DLL missing. Forcing update to version: %s (from %s)\n", version.c_str(), url.c_str());
			} else {
				Logger::Info("launcher is already up-to-date.\n");
				return UPDATE_ALREADY_UP_TO_DATE;
			}
			break;

		case lu::SELF_UPDATE_CHECK_STEAM_METHOD_FAILED:
			Logger::Error("Error while checking for updates, and failed to use steam as a backup. Passing along to the launcher to try again using the Steam API.\n");
			return UPDATE_CHECK_STEAM_METHOD_FAILED;

		case lu::SELF_UPDATE_CHECK_ERR_GENERIC:
			SetCurrentUpdaterState(UPDATER_FAILED);
			ShowMessageBox(mainWindow, "An error was encountered while checking for the availability of updates. Check the log file for more details.\n", MB_ICONERROR);
			return UPDATE_ERROR;

		case lu::SELF_UPDATE_CHECK_UPDATE_AVAILABLE:
			Logger::Info("Found available update version: %s (from %s)\n", version.c_str(), url.c_str());
			break;

		default:
			SetCurrentUpdaterState(UPDATER_FAILED);
			ShowMessageBox(mainWindow, "An unknown error was encountered while checking for the availability of updates. Check the log file for more details.\n", MB_ICONERROR);
			Logger::Error("Recieved unexpected error code %d while checking for availability of self updates. Please report this as a bug.\n", checkResult);
			return UPDATE_ERROR;
		}
	}

	// Check for a renamed updater exe from a previous update and try to delete it if found.
	// Since we have the lockfile to prevent multiple instances of the updater running at the same time,
	// we shouldn't ever fail to delete it, but you never know...
	if (!TryDeleteOldRenamedUpdaterExe(mainWindow)) {
		return UPDATE_ERROR;
	}

	if (!skipConfirmation) {
		SetCurrentUpdaterState(UPDATER_PROMPTING_USER);

		// An appropriate update is available. Prompt the user about it.
		const std::string updatePrompt = (std::ostringstream() << "An update is available for the launcher.\n" <<
			"It will update from version " << Launcher::LAUNCHER_VERSION << " to version " << version << ".\n\n" <<
			"Do you want to update now?").str();

		int updatePromptResult = ShowMessageBox(mainWindow, updatePrompt.c_str(), MB_ICONINFORMATION | MB_YESNO);
		if (updatePromptResult != IDOK && updatePromptResult != IDYES) {
			Logger::Info("Launcher updated rejected by user\n");
			return UPDATE_SKIPPED;
		}
	}

	SetCurrentUpdaterState(UPDATER_DOWNLOADING_UPDATE);

	Logger::Info("Starting update download from %s\n", url.c_str());

	std::string updateZipFilename;

	if (!updateManager.DownloadUpdate(url, updateZipFilename)) {
		SetCurrentUpdaterState(UPDATER_FAILED);
		ShowMessageBox(mainWindow, "Failed to download the update for the launcher. Check the log file for more details.\n", MB_ICONERROR);
		Logger::Error("Error trying to download update from URL: %s\n", url.c_str());
		return UPDATE_ERROR;
	}

	if (updateZipFilename.empty()) {
		SetCurrentUpdaterState(UPDATER_FAILED);
		ShowMessageBox(mainWindow, "Failed to download the update for the launcher. Check the log file for more details.\n", MB_ICONERROR);
		Logger::Error("No zip file found after downloading update from URL: %s\n", url.c_str());
		return UPDATE_ERROR;
	}

	Logger::Info("Downloaded update zip file: %s\n", updateZipFilename.c_str());

	SetCurrentUpdaterState(UPDATER_INSTALLING_UPDATE);

	// Check if the launcher dlls are locked. If so, prompt the user about it.
	// Files could remain locked briefly after termination, so we check this after the download.
	if (!VerifyLauncherNotRunning(mainWindow)) {
		return UPDATE_ERROR;
	}

	if (std::filesystem::exists(LauncherDataBackupFolder)) {
		Logger::Info("Deleting backup of previous version...\n");
		std::filesystem::remove_all(LauncherDataBackupFolder);
	}
	if (std::filesystem::exists(LauncherDataFolder)) {
		Logger::Info("Backing up current version...\n");
		std::filesystem::rename(LauncherDataFolder, LauncherDataBackupFolder);
	}
	Logger::Info("Initializing directory for new version...\n");
	std::filesystem::create_directories(LauncherDataFolder);
	std::ofstream((const char*)UnfinishedUpdateMarker);  // Creates an empty file.

	// The updater cannot directly overwrite its own currently-running exe with the new one from the update.
	// To get around this, we do the following:
	//   1. Rename the existing exe as `exe.bak`. Windows lets us rename a file thats in use, but the file remains locked until this program ends.
	//   2. Make a copy of the existing exe under the original name, just in case we encounter an error during unpacking and can't write the new exe.
	// So the result of this is that the running exe has been renamed as `exe.bak` (and is still locked by windows),
	// AND we have a copy under the original name (which is not locked and can be overwritten).
	if (std::filesystem::exists(UpdaterExeFilename)) {
		Logger::Info("Renaming exe...\n");
		std::filesystem::rename(UpdaterExeFilename, RenamedUpdaterExeFilename);
		Logger::Info("Copying exe...\n");
		try {
			std::filesystem::copy(RenamedUpdaterExeFilename, UpdaterExeFilename);
		} catch (std::filesystem::filesystem_error& err) {
			Logger::Error("Error trying to create copy of own executable: %s", err.what());
			// Ahhh! Attempt to recover.
			std::filesystem::rename(RenamedUpdaterExeFilename, UpdaterExeFilename);
			return UPDATE_ERROR;
		}
	}

	if (!Zip::ExtractAllToFolder(updateZipFilename.c_str(), ".")) {
		SetCurrentUpdaterState(UPDATER_FAILED);
		ShowMessageBox(mainWindow, "Unable to extract the update, check updater.log\n", MB_ICONERROR);
		Logger::Error("Error while extracting archive\n");
		return UPDATE_ERROR;
	}

	Logger::Info("Deleting 'unfinished update' marker...\n");
	std::filesystem::remove(UnfinishedUpdateMarker);

	Logger::Info("Update successful!\n");
	return UPDATE_SUCCESSFUL;
}

void SetWorkingDirToExe() {
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
	SetCurrentDirectoryA(exeDir.string().c_str());
}

std::string Updater::BuildCleanCli(int argc, char** argv, bool addCheckSelfUpdate) {
	std::ostringstream cli;
	cli << UpdaterExeFilename;
	for (int i = 0; i < argc; ++i) {
		if (strcmp(argv[i], ForcedArg) == 0 || strcmp(argv[i], CheckSelfUpdateLauncherArg) == 0) {
			continue;
		}
		if (strcmp(argv[i], ReleaseURL) == 0 || strcmp(argv[i], UpgradeVersion) == 0 || strcmp(argv[i], LauncherProcessIdArg) == 0) {
			i++;
			continue;
		}
		cli << " " << argv[i];
	}
	if (addCheckSelfUpdate) {
		cli << " " << CheckSelfUpdateLauncherArg;
	}
	return cli.str();
}

typedef int(__stdcall* LAUNCHERPROC)(int argc, char** argv);

int Updater::RunLauncher(HWND mainWindow, int argc, char** argv, bool checkUpdates) {
	Updater::SetCurrentUpdaterState(Updater::UPDATER_STARTING_LAUNCHER);

	Logger::Info("Preparing to start launcher...\n");

	std::filesystem::path launcherDllFullPath(LauncherDllPath);

	try {
		launcherDllFullPath = std::filesystem::absolute(launcherDllFullPath);
	} catch (std::filesystem::filesystem_error const& ex) {
		std::string err = "Filesystem error locating REPENTOGONLauncherApp.dll: " + std::string(ex.what()) + "\n";
		ShowMessageBox(mainWindow, err.c_str(), MB_ICONERROR);
		Logger::Error(err.c_str());
		return -1;
	}

	HINSTANCE launcher = LoadLibraryExA(launcherDllFullPath.string().c_str(), NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (launcher == NULL) {
		std::string err = "Failed to load REPENTOGONLauncherApp.dll: " + std::to_string(GetLastError()) + "\n";
		ShowMessageBox(mainWindow, err.c_str(), MB_ICONERROR);
		Logger::Error(err.c_str());
		return -1;
	}

	LAUNCHERPROC proc = (LAUNCHERPROC)GetProcAddress(launcher, "_StartLauncherApp@8");
	if (proc == NULL) {
		std::string err = "Failed to start launcher: " + std::to_string(GetLastError()) + "\n";
		ShowMessageBox(mainWindow, err.c_str(), MB_ICONERROR);
		Logger::Error(err.c_str());
		return -1;
	}

	const std::string cli = BuildCleanCli(argc, argv, checkUpdates);
	int newargc = 0;
	char** newargv = Utils::CommandLineToArgvA(cli.c_str(), &newargc);

	SetCurrentUpdaterState(UPDATER_RUNNING_LAUNCHER);
	Logger::Info("Starting launcher with cli: %s\n", cli.c_str());
	SetForegroundWindow(mainWindow);
	int res = proc(newargc, newargv);
	Logger::Info("Launcher application closed with return code %d\n", res);
	Utils::FreeCli(newargc, newargv);
	FreeLibrary(launcher);

	return res;
}

bool Updater::Restart(int argc, char** argv) {
	SetCurrentUpdaterState(UPDATER_SHUTTING_DOWN);

	STARTUPINFOA startup;
	memset(&startup, 0, sizeof(startup));

	PROCESS_INFORMATION info;
	memset(&info, 0, sizeof(info));

	std::string cli = BuildCleanCli(argc, argv, false);

	Logger::Info("Restarting: %s\n", cli.c_str());
	return CreateProcessA(UpdaterExeFilename, (LPSTR)cli.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR cli, int) {
	SetWorkingDirToExe();
	Logger::Init("updater.log", true);
	Logger::Info("Updater started with command-line args: %s\n", cli);

	int argc = 0;
	char** argv = Updater::Utils::CommandLineToArgvA(cli, &argc);

	// Open a handle for the prior launcher process, if provided, and kill it.
	if (HANDLE launcherHandle = Updater::TryOpenLauncherProcessHandle(argc, argv); launcherHandle != INVALID_HANDLE_VALUE) {
		Logger::Info("Restart detected. Terminating previous launcher instance...\n");
		if (TerminateProcess(launcherHandle, 0)) {
			WaitForSingleObject(launcherHandle, INFINITE);
		} else {
			// We can continue on after this and hope for the best.
			Logger::Error("Failed to terminate launcher process (%d)\n", GetLastError());
		}
		CloseHandle(launcherHandle);
	}

	// Clean up any legacy files lying around from old versions.
	for (const std::string& filename : Updater::LegacyCleanupFiles) {
		if (std::filesystem::exists(filename)) {
			Logger::Info("Deleting legacy file %s...\n", filename.c_str());
			try {
				std::filesystem::remove(filename);
			} catch (const std::filesystem::filesystem_error& err) {
				Logger::Error("Error trying to delete legacy file: %s\n", err.what());
			}
		}
	}

	sGithubExecutor->Start();

	// Create a dummy window to use as a parent for popups.
	Updater::UniqueWindow mainWindow(NULL, "STATIC", NULL, NULL, CW_USEDEFAULT, CW_USEDEFAULT, 1, 1, hInstance);
	
	// Get the center of the appropriate monitor.
	HMONITOR monitor = MonitorFromWindow(mainWindow, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitorInfo{ sizeof(monitorInfo) };
	GetMonitorInfo(monitor, &monitorInfo);
	POINT screenCenter{ (monitorInfo.rcWork.left + monitorInfo.rcWork.right) / 2, (monitorInfo.rcWork.top + monitorInfo.rcWork.bottom) / 2 };

	// Start up a progress bar window on a separate thread so that we can show some not-frozen UI to the user.
	std::thread progressBarThread(Updater::ProgressBarThread, screenCenter);
	progressBarThread.detach();

	Updater::UpdateLauncherResult result = Updater::TryUpdateLauncher(argc, argv, mainWindow);

	int res = -1;

	switch (result) {
	case Updater::UPDATE_ALREADY_UP_TO_DATE:
	case Updater::UPDATE_SKIPPED:
	case Updater::UPDATE_CHECK_STEAM_METHOD_FAILED:
		// Run the launcher!
		res = Updater::RunLauncher(mainWindow, argc, argv, result == Updater::UPDATE_CHECK_STEAM_METHOD_FAILED);
		break;

	case Updater::UPDATE_SUCCESSFUL:
		// Restart before starting the launcher in case we made changes to the updater.
		res = !Updater::Restart(argc, argv);
		break;

	case Updater::UPDATE_ERROR:
		res = -1;
		break;

	default:
		Logger::Error("Unexpected update result value: %d", result);
		res = -1;
		break;
	}

	Updater::SetCurrentUpdaterState(Updater::UPDATER_SHUTTING_DOWN);

	sGithubExecutor->Stop();

	return res;
}