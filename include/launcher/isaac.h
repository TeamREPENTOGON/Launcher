#pragma once

namespace IsaacInterface {
	// LevelStage
	static constexpr const unsigned int STAGE_NULL = 0;
	static constexpr const unsigned int STAGE4_1 = 7;
	static constexpr const unsigned int STAGE4_2 = 8;
	static constexpr const unsigned int STAGE4_3 = 9;
	static constexpr const unsigned int STAGE8 = 13;
	static constexpr const unsigned int NUM_STAGES = 14;

	// StageType
	static constexpr const unsigned int STAGETYPE_ORIGINAL = 0;
	static constexpr const unsigned int STAGETYPE_GREEDMODE = 3;
	static constexpr const unsigned int STAGETYPE_REPENTANCE_B = 5;

	extern const char* levelNames[];
	extern const char* uniqueLevelNames[];
}

