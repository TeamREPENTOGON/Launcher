#include <Windows.h>
#include <WinBase.h>
#include <filesystem>

#include "comm/messages.h"
#include "self_updater/self_updater.h"
#include "self_updater/logger.h"
#include "self_updater/utils.h"
#include "self_updater/launcher_update_manager.h"
#include "self_updater/unpacker.h"
#include "launcher/version.h"

const char* Updater::ForcedArg = "--force";
const char* Updater::UnstableArg = "--unstable";
const char* Updater::UrlArg = "--url";

namespace Updater {
	static const char* LauncherExePath = "./REPENTOGONLauncher.exe";
	static const char* UpdaterProcessName = "REPENTOGON Launcher Updater";
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

	int argc = 0;
	char** argv = Updater::Utils::CommandLineToArgvA(cli, &argc);
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

	if (std::filesystem::exists(Updater::LauncherExePath)) {
		// Check if the launcher exe is locked, if so, wait for it to close
		// TODO: Improve the GUI for this step
		bool sawLauncherExeLocked = false;
		while (true) {
			HANDLE fh = CreateFileA(Updater::LauncherExePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
			if (fh == INVALID_HANDLE_VALUE) {
				if (!sawLauncherExeLocked) {
					sawLauncherExeLocked = true;
					Logger::Info("The launcher is currently running. Waiting for it to close...\n");
					std::this_thread::sleep_for(std::chrono::milliseconds(2000));
					continue;
				}
				int response = MessageBoxA(NULL, "Cannot update the REPENTOGON Launcher, as it is currently running.", Updater::UpdaterProcessName, MB_ICONINFORMATION | MB_RETRYCANCEL);
				if (response == IDRETRY || response == IDOK || response == IDTRYAGAIN) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1000));
					continue;
				}
				Logger::Fatal("The launcher is currently running, and the user chose to cancel waiting.\n");
				return -1;
			} else {
				CloseHandle(fh);
				break;
			}
		}
	}

	// TODO: Properly hook up logging from LauncherUpdateManager
	NopLogGUI gui;
	using lu = Updater::LauncherUpdateManager;
	lu updateManager(&gui);
	
	std::string url;

	char* cliUrl = Updater::Utils::GetUrl(argc, argv);
	if (cliUrl) {
		url = std::string(cliUrl);
		Logger::Info("Downloading update from provided URL: %s\n", url.c_str());
	} else {
		bool allowUnstable = Updater::Utils::AllowUnstable(argc, argv);
		if (allowUnstable) {
			Logger::Info("Fetching unstable releases is enabled\n");
		}
		std::string version;
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
			Logger::Info("Downloading version: %s (from %s)\n", version.c_str(), url.c_str());
			break;

		default: {
			Logger::Fatal("Recieved unexpected error code %d while checking for availability of self updates. Please report this as a bug.\n", checkResult);
			return -1;
		}
		}
	}

	if (!updateManager.DownloadAndExtractUpdate(url.c_str())) {
		MessageBoxA(NULL, "Failed to download the update for the launcher. Check the log file for more details.\n", Updater::UpdaterProcessName, MB_ICONERROR);
		Logger::Fatal("Error trying to download update from URL: %s\n", url.c_str());
		return -1;
	}

	if (!Unpacker::ExtractArchive(Comm::UnpackedArchiveName)) {
		MessageBoxA(NULL, "Unable to unpack the update, check log file\n", Updater::UpdaterProcessName, MB_ICONERROR);
		Logger::Fatal("Error while extracting archive\n");
		return -1;
	}

	if (!DeleteFileA(Comm::UnpackedArchiveName)) {
		MessageBoxA(NULL, "Unable to delete the archive containing the update.\n"
			"Delete the archive (launcher_update.bin) then you can start the launcher\n", Updater::UpdaterProcessName, MB_ICONERROR);
		Logger::Fatal("Error while deleting archive (%d)\n", GetLastError());
		return -1;
	}

	Logger::Info("Update successful. Starting launcher...\n");
	Updater::StartLauncher();

	return 0;
}