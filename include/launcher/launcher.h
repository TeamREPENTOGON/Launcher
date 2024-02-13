#pragma once

enum LaunchMode {
	LAUNCH_MODE_VANILLA,
	LAUNCH_MODE_REPENTOGON
};

struct IsaacOptions {
	LaunchMode mode;

	// Repentogon options
	bool console;

	// Game options
	bool lua_debug;
	int level_stage;
	int stage_type;
	int lua_heap_size;
};