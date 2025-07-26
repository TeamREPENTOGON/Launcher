#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include "launcher/windows/launcher.h"

namespace Launcher {
	void HandleSelfUpdate(LauncherMainWindow* mainWindow, bool allowUnstable, bool force);
}