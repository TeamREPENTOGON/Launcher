#pragma once

#include "inih/cpp/INIReader.h"
#include "launcher/installation.h"
#include "shared/loggable_gui.h"

namespace Launcher {
	enum LaunchMode {
		LAUNCH_MODE_VANILLA,
		LAUNCH_MODE_REPENTOGON
	};

	struct IsaacOptions {
		LaunchMode mode;
		bool unstableUpdates;

		// Repentogon options
		bool console;
		bool update;

		// Game options
		bool luaDebug;
		int levelStage;
		int stageType;
		int luaHeapSize;

		void InitializeDefaults(ILoggableGUI* gui, bool allowUpdates, bool allowUnstableUpdates, bool validRepentogon);
		void InitializeFromConfig(ILoggableGUI* gui, INIReader& reader, bool validRepentogon);
		void WriteConfiguration(ILoggableGUI* gui, Launcher::Installation const& installation);
	};

	int Launch(const char* path, IsaacOptions const& options);
}