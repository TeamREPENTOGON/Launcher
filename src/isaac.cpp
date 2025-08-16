#include "launcher/isaac.h"

#include <vector>

namespace IsaacInterface {
	const std::vector<Stage> stages = {
		{ STAGE1_1, STAGETYPE_ORIGINAL, "Basement I" },
		{ STAGE1_2, STAGETYPE_ORIGINAL, "Basement II" },
		{ STAGE1_1, STAGETYPE_WOTL, "Cellar I" },
		{ STAGE1_2, STAGETYPE_WOTL, "Cellar II" },
		{ STAGE1_1, STAGETYPE_AFTERBIRTH, "Burning Basement I" },
		{ STAGE1_2, STAGETYPE_AFTERBIRTH, "Burning Basement II" },
		{ STAGE1_1, STAGETYPE_REPENTANCE, "Downpour I" },
		{ STAGE1_2, STAGETYPE_REPENTANCE, "Downpour II" },
		{ STAGE1_1, STAGETYPE_REPENTANCE_B, "Dross I" },
		{ STAGE1_2, STAGETYPE_REPENTANCE_B, "Dross II" },

		{ STAGE2_1, STAGETYPE_ORIGINAL, "Caves I" },
		{ STAGE2_2, STAGETYPE_ORIGINAL, "Caves II" },
		{ STAGE2_1, STAGETYPE_WOTL, "Catacombs I" },
		{ STAGE2_2, STAGETYPE_WOTL, "Catacombs II" },
		{ STAGE2_1, STAGETYPE_AFTERBIRTH, "Flooded Caves I" },
		{ STAGE2_2, STAGETYPE_AFTERBIRTH, "Flooded Caves II" },
		{ STAGE2_1, STAGETYPE_REPENTANCE, "Mines I" },
		{ STAGE2_2, STAGETYPE_REPENTANCE, "Mines II" },
		{ STAGE2_1, STAGETYPE_REPENTANCE_B, "Ashpit I" },
		{ STAGE2_2, STAGETYPE_REPENTANCE_B, "Ashpit II" },

		{ STAGE3_1, STAGETYPE_ORIGINAL, "Depths I" },
		{ STAGE3_2, STAGETYPE_ORIGINAL, "Depths II" },
		{ STAGE3_1, STAGETYPE_WOTL, "Necropolis I" },
		{ STAGE3_2, STAGETYPE_WOTL, "Necropolis II" },
		{ STAGE3_1, STAGETYPE_AFTERBIRTH, "Dank Depths I" },
		{ STAGE3_2, STAGETYPE_AFTERBIRTH, "Dank Depths II" },
		{ STAGE3_1, STAGETYPE_REPENTANCE, "Mausoleum I" },
		{ STAGE3_2, STAGETYPE_REPENTANCE, "Mausoleum II" },
		{ STAGE3_1, STAGETYPE_REPENTANCE_B, "Gehenna I" },
		{ STAGE3_2, STAGETYPE_REPENTANCE_B, "Gehenna II" },

		{ STAGE4_1, STAGETYPE_ORIGINAL, "Womb I" },
		{ STAGE4_2, STAGETYPE_ORIGINAL, "Womb II" },
		{ STAGE4_1, STAGETYPE_WOTL, "Utero I" },
		{ STAGE4_2, STAGETYPE_WOTL, "Utero II" },
		{ STAGE4_1, STAGETYPE_AFTERBIRTH, "Scarred Womb I" },
		{ STAGE4_2, STAGETYPE_AFTERBIRTH, "Scarred Womb II" },
		{ STAGE4_1, STAGETYPE_REPENTANCE, "Corpse I" },
		{ STAGE4_2, STAGETYPE_REPENTANCE, "Corpse II" },

		{ STAGE4_3, STAGETYPE_ORIGINAL, "???" },

		{ STAGE5, STAGETYPE_ORIGINAL, "Sheol" },
		{ STAGE5, STAGETYPE_WOTL, "Cathedral" },

		{ STAGE6, STAGETYPE_ORIGINAL, "Dark Room" },
		{ STAGE6, STAGETYPE_WOTL, "Chest" },

		{ STAGE7, STAGETYPE_ORIGINAL, "The Void" },

		{ STAGE8, STAGETYPE_ORIGINAL, "Home" },
		{ STAGE8, STAGETYPE_WOTL, "Home (night)" },
	};

	const Stage* GetStage(const int levelStage, const int stageType) {
		for (const Stage& stage : stages) {
			if (stage.level == levelStage && stage.type == stageType) {
				return &stage;
			}
		}
		return nullptr;
	}

	bool IsValidStage(const int levelStage, const int stageType) {
		return GetStage(levelStage, stageType) != nullptr;
	}

	const char* GetStageName(const int levelStage, const int stageType) {
		const Stage* stage = GetStage(levelStage, stageType);
		if (stage) {
			return stage->name;
		}
		return nullptr;
	}
}
