#pragma once

#include "inih/cpp/INIReader.h"
#include "launcher/installation.h"
#include "shared/loggable_gui.h"

namespace Launcher {
	struct IsaacOptions;
	int Launch(ILoggableGUI* gui, const char* path, bool isLegacy, LauncherConfiguration const* configuration);
}