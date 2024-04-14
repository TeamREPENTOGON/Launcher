#pragma once

enum LaunchMode {
	LAUNCH_MODE_VANILLA,
	LAUNCH_MODE_REPENTOGON
};

struct IsaacOptions {
	LaunchMode mode;

	// Repentogon options
	bool console;
	bool update;

	// Game options
	bool luaDebug;
	int levelStage;
	int stageType;
	int luaHeapSize;
};

namespace Launcher {
	static constexpr const char* version = "alpha";
}

void Launch(IsaacOptions const& options);