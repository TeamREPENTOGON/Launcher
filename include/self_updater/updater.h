#pragma once

#include <cstdio>

#include <string>

#include "shared/monitor.h"

namespace Updater {
	enum UpdateState {
		/* Launcher has created the updater, and is ready to be updated. */
		UPDATE_STATE_INIT,
	};

	enum UpdateNotificationType {

	};

	struct UpdateNotification {
		UpdateNotificationType type;
		void* data;
	};

	namespace Updater {
		class UpdaterBackend {
		public:
			UpdaterBackend(const char* lockFile, const char* from, const char* to);

		private:
			FILE* _lockFile;
			std::string _fromVersion;
			std::string _toVersion;

			UpdateState _state;
			Threading::Monitor<UpdateNotification>* _monitor;
		};
	}
}