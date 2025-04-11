#pragma once

#include "inih/cpp/INIReader.h"
#include "launcher/installation.h"
#include "shared/loggable_gui.h"

namespace Launcher {
	enum LaunchMode {
		LAUNCH_MODE_VANILLA,
		LAUNCH_MODE_REPENTOGON
	};

	namespace Configuration {
		template<typename T>
		using ConfigurationTuple = std::tuple<std::string, std::string, T>;

		ConfigurationTuple<bool> HasConsole();
		ConfigurationTuple<int> LevelStage();
		ConfigurationTuple<int> StageType();
		ConfigurationTuple<bool> HasLuaDebug();
		ConfigurationTuple<int> LuaHeapSize();
		ConfigurationTuple<bool> HasAutomaticUpdates();
		ConfigurationTuple<bool> HasUnstableUpdates();
	}

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
		void WriteConfiguration(ILoggableGUI* gui, Launcher::Installation& installation);
	};

	int Launch(ILoggableGUI* gui, const char* path, bool isLegacy, IsaacOptions const& options);
}