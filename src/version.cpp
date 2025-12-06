#include "launcher/version.h"

namespace Launcher {

#ifndef CMAKE_LAUNCHER_VERSION
#pragma message("CMAKE_LAUNCHER_VERSION undefined")
	const char* LAUNCHER_VERSION = "dev";
#else
	const char* LAUNCHER_VERSION = CMAKE_LAUNCHER_VERSION;
#endif
}