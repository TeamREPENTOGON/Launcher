#include <string>

#include "shared/logger.h"
#include "shared/launcher_update_checker.h"
#include "launcher/launcher_self_update.h"

namespace Launcher {
	static const char* SelfUpdaterExePath = "./REPENTOGONLauncherUpdater.exe";

	bool StartUpdater(const std::string& url) {
		std::string cli(SelfUpdaterExePath);
		cli += " --url \"";
		cli += url;
		cli += "\"";

		PROCESS_INFORMATION info;
		memset(&info, 0, sizeof(info));

		STARTUPINFOA startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));

		BOOL ok = CreateProcessA(SelfUpdaterExePath, &cli[0], NULL, NULL, false, 0, NULL, NULL, &startupInfo, &info);
		if (!ok) {
			return false;
		}

		return true;
	}
}