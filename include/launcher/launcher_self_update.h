#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include "launcher/windows/launcher.h"

namespace Launcher {
	void HandleSelfUpdate(MainFrame* mainWindow, bool allowUnstable, bool force);
}