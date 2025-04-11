#include <cstdio>
#include <string>

#include "launcher/configuration.h"
#include "launcher/isaac.h"

namespace Defaults {
	constexpr const bool console = false;
	constexpr const int levelStage = 0;
	constexpr const int stageType = 0;
	constexpr const bool luaDebug = false;
	constexpr const int luaHeapSize = 1024;
	constexpr const bool update = true;
	constexpr const bool unstableUpdates = false;
}

namespace Sections {
	const std::string repentogon("Repentogon");
	const std::string vanilla("Vanilla");
	const std::string shared("Shared");
}

namespace Keys {
	const std::string console("Console");
	const std::string levelStage("LevelStage");
	const std::string luaDebug("LuaDebug");
	const std::string luaHeapSize("LuaHeapSize");
	const std::string stageType("StageType");
	const std::string launchMode("LaunchMode");
	const std::string update("Update");
	const std::string unstableUpdates("UnstableUpdates");
}

namespace Launcher::Configuration {
	ConfigurationTuple<bool> HasConsole() {
		return { Sections::repentogon, Keys::console, Defaults::console };
	}

	ConfigurationTuple<int> LevelStage() {
		return { Sections::vanilla, Keys::levelStage, Defaults::levelStage };
	}

	ConfigurationTuple<bool> HasLuaDebug() {
		return { Sections::vanilla, Keys::luaDebug, Defaults::luaDebug };
	}

	ConfigurationTuple<int> StageType() {
		return { Sections::vanilla, Keys::stageType, Defaults::stageType};
	}

	ConfigurationTuple<int> LuaHeapSize() {
		return { Sections::vanilla, Keys::luaHeapSize, Defaults::luaHeapSize };
	}

	ConfigurationTuple<bool> HasAutomaticUpdates() {
		return { Sections::repentogon, Keys::update, Defaults::update };
	}

	ConfigurationTuple<bool> HasUnstableUpdates() {
		return { Sections::repentogon, Keys::unstableUpdates, Defaults::unstableUpdates };
	}
}

bool ReadBoolean(INIReader& reader, Launcher::Configuration::ConfigurationTuple<bool>(*fn)()) {
	auto [section, key, def] = fn();
	return reader.GetBoolean(section, key, def);
}

int ReadInteger(INIReader& reader, Launcher::Configuration::ConfigurationTuple<int>(*fn)()) {
	auto [section, key, def] = fn();
	return reader.GetInteger(section, key, def);
}

void Launcher::IsaacOptions::InitializeDefaults(ILoggableGUI* gui, bool allowUpdates, bool allowUnstableUpdates, bool validRepentogon) {
	console = Defaults::console;
	levelStage = Defaults::levelStage;
	luaDebug = Defaults::luaDebug;
	luaHeapSize = Defaults::luaHeapSize;
	mode = validRepentogon ? LAUNCH_MODE_REPENTOGON : LAUNCH_MODE_VANILLA;
	stageType = Defaults::stageType;
	update = allowUpdates;
	unstableUpdates = allowUnstableUpdates;
}

void Launcher::IsaacOptions::InitializeFromConfig(ILoggableGUI* gui, INIReader& reader, bool validRepentogon) {
	namespace c = Configuration;

	console = ReadBoolean(reader, c::HasConsole);
	levelStage = ReadInteger(reader, c::LevelStage);
	luaDebug = ReadBoolean(reader, c::HasLuaDebug);
	luaHeapSize = ReadInteger(reader, c::LuaHeapSize);
	stageType = ReadInteger(reader, c::StageType);
	mode = (LaunchMode)reader.GetInteger(Sections::shared, Keys::launchMode, LAUNCH_MODE_REPENTOGON);
	update = ReadBoolean(reader, c::HasAutomaticUpdates);
	unstableUpdates = ReadBoolean(reader, c::HasUnstableUpdates);

	if (mode != LAUNCH_MODE_REPENTOGON && mode != LAUNCH_MODE_VANILLA) {
		gui->LogWarn("Invalid value %d for %s field in repentogon_launcher.ini. Overriding with default", mode, Keys::launchMode);
		if (validRepentogon) {
			mode = LAUNCH_MODE_REPENTOGON;
		} else {
			mode = LAUNCH_MODE_VANILLA;
		}
	}

	if (luaHeapSize < 0) {
		gui->LogWarn("Invalid value %d for %s field in repentogon_launcher.ini. Overriding with default", luaHeapSize, Keys::luaHeapSize.c_str());
		luaHeapSize = Defaults::luaHeapSize;
	}

	if (levelStage < IsaacInterface::STAGE_NULL || levelStage > IsaacInterface::STAGE8) {
		gui->LogWarn("Invalid value %d for %s field in repentogon_launcher.ini. Overriding with default", levelStage, Keys::levelStage.c_str());
		levelStage = Defaults::levelStage;
		stageType = Defaults::stageType;
	}

	if (stageType < IsaacInterface::STAGETYPE_ORIGINAL || stageType > IsaacInterface::STAGETYPE_REPENTANCE_B) {
		gui->LogWarn("Invalid value %d for %s field in repentogon_launcher.ini. Overriding with default", stageType, Keys::stageType.c_str());
		stageType = Defaults::stageType;
	}

	if (stageType == IsaacInterface::STAGETYPE_GREEDMODE) {
		gui->LogWarn("Value 3 (Greed mode) for %s field in repentogon_launcher.ini is deprecated since Repentance."
			"Overriding with default", Keys::stageType.c_str());
		stageType = Defaults::stageType;
	}

	// Validate stage type for Chapter 4.
	if (levelStage == IsaacInterface::STAGE4_1 || levelStage == IsaacInterface::STAGE4_2) {
		if (stageType == IsaacInterface::STAGETYPE_REPENTANCE_B) {
			gui->LogWarn("Invalid value %d for %s field associated with value %d "
				"for %s field in repentogon_launcher.ini. Overriding with default", stageType, Keys::levelStage.c_str(),
				stageType, Keys::stageType.c_str());
			stageType = Defaults::stageType;
		}
	}

	// Validate stage type for Chapters > 4.
	if (levelStage >= IsaacInterface::STAGE4_3 && levelStage <= IsaacInterface::STAGE8) {
		if (stageType != IsaacInterface::STAGETYPE_ORIGINAL) {
			gui->LogWarn("Invalid value %d for %s field associated with value %d "
				"for %s field in repentogon_launcher.ini. Overriding with default", levelStage, Keys::levelStage.c_str(),
				stageType, Keys::stageType.c_str());
			stageType = Defaults::stageType;
		}
	}
}

void Launcher::IsaacOptions::WriteConfiguration(ILoggableGUI* gui, Launcher::Installation& installation) {
	LauncherConfiguration* configuration = installation.GetLauncherConfiguration();
	std::string filename = configuration->GetConfigurationPath();
	if (filename.empty()) {
		gui->LogError("No launcher configuration file found previously, cannot save settings\n");
		return;
	}

	FILE* f = fopen(filename.c_str(), "w");
	if (!f) {
		gui->LogError("Unable to open file to write launcher configuration, skipping");
		return;
	}

	std::string isaacExecutable = configuration->GetIsaacExecutablePath();
	fprintf(f, "[%s]\n", Launcher::Configuration::GeneralSection);
	fprintf(f, "%s = %s\n", Launcher::Configuration::IsaacExecutableKey, isaacExecutable.empty() ?
		Launcher::Configuration::EmptyPath : isaacExecutable.c_str());

	fprintf(f, "[%s]\n", Sections::repentogon.c_str());
	fprintf(f, "%s = %d\n", Keys::console.c_str(), console);
	fprintf(f, "%s = %d\n", Keys::update.c_str(), update);
	fprintf(f, "%s = %d\n", Keys::unstableUpdates.c_str(), unstableUpdates ? 1 : 0);

	fprintf(f, "[%s]\n", Sections::vanilla.c_str());
	fprintf(f, "%s = %d\n", Keys::levelStage.c_str(), levelStage);
	fprintf(f, "%s = %d\n", Keys::stageType.c_str(), stageType);
	fprintf(f, "%s = %d\n", Keys::luaHeapSize.c_str(), luaHeapSize);
	fprintf(f, "%s = %d\n", Keys::luaDebug.c_str(), luaDebug);

	fprintf(f, "[%s]\n", Sections::shared.c_str());
	fprintf(f, "%s = %d\n", Keys::launchMode.c_str(), mode);

	fclose(f);

	installation.GetLauncherConfiguration()->Invalidate();
}