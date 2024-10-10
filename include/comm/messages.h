#pragma once

namespace Comm {
	constexpr const char* PipeName = "\\\\.\\pipe\\repentogon_launcher_updater";

	constexpr const char* UpdaterHello = "Updater-Hello";
	constexpr const char* LauncherHello = "Launcher-Hello";
	constexpr const char* UpdaterRequestPID = "Updater-RequestPID";
	constexpr const char* LauncherAnswerPID = "Launcher-PID";
}