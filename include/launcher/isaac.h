#pragma once

#include <vector>

namespace IsaacInterface {
	enum LevelStage {
		STAGE_NULL = 0,
		STAGE1_1 = 1,
		STAGE1_2 = 2,
		STAGE2_1 = 3,
		STAGE2_2 = 4,
		STAGE3_1 = 5,
		STAGE3_2 = 6,
		STAGE4_1 = 7,
		STAGE4_2 = 8,
		STAGE4_3 = 9,
		STAGE5 = 10,
		STAGE6 = 11,
		STAGE7 = 12,
		STAGE8 = 13,
		NUM_STAGES = 14
	};

	enum StageType {
		STAGETYPE_ORIGINAL = 0,
		STAGETYPE_WOTL = 1,
		STAGETYPE_AFTERBIRTH = 2,
		STAGETYPE_GREEDMODE = 3,
		STAGETYPE_REPENTANCE = 4,
		STAGETYPE_REPENTANCE_B = 5,
	};

	struct Stage {
		LevelStage level;
		StageType type;
		const char* name;
	};

	extern const std::vector<Stage> stages;

	// Returns true if the given LevelStage/StageType combination corresponds to a valid stage.
	bool IsValidStage(const int levelStage, const int stageType);

	// If the given LevelStage/StageType corresponds to a valid stage, returns the name of that stage. Otherwise, returns nullptr.
	const char* GetStageName(const int levelStage, const int stageType);
}
