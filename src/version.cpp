#include "launcher/version.h"

namespace Launcher {

#ifndef CMAKE_LAUNCHER_VERSION
#pragma message("CMAKE_LAUNCHER_VERSION undefined")
	const char* version = "dev";
#else
	const char* version = CMAKE_LAUNCHER_VERSION;
#endif
}