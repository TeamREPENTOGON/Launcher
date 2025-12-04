#pragma once

#include <memory>
#include <string>
#include <vector>

#include "shared/loggable_gui.h"
#include "shared/scoped_module.h"

static constexpr const char* defaultRepentogonFolder = "repentogon";

namespace Libraries {
	static constexpr const char* loader = "zhlLoader.dll";
	static constexpr const char* zhl = "libzhl.dll";
	static constexpr const char* repentogon = "zhlRepentogon.dll";
}

namespace Symbols {
	static constexpr const char* zhlVersion = "__ZHL_VERSION";
	static constexpr const char* repentogonVersion = "__REPENTOGON_VERSION";
	static constexpr const char* loaderVersion = "__ZHL_LOADER_VERSION";
}

namespace Configuration {
	// Sections
	static constexpr const char* GeneralSection = "General";

	// Keys (General section)
	static constexpr const char* IsaacExecutableKey = "IsaacExecutable";
	static constexpr const char* RepentogonFolderKey = "RepentogonFolder";

	// Default values (General section)
	static constexpr const char* EmptyPath = "__empty__";
}

enum LoadableDlls {
	LOADABLE_DLL_ZHL_LOADER,
	LOADABLE_DLL_LIBZHL,
	LOADABLE_DLL_REPENTOGON,
	LOADABLE_DLL_MAX
};

enum LoadDLLState {
	LOAD_DLL_STATE_NONE,
	LOAD_DLL_STATE_OK,
	LOAD_DLL_STATE_FAIL
};

/* Pair <string, bool> indicating whether a file was found on the fileystem. */
struct FoundFile {
	std::string filename;
	bool found;
};

/* State of the installation of Repentogon, if any. */
enum RepentogonInstallationStatus {
	/* No installation (neutral state). */
	REPENTOGON_INSTALLATION_STATUS_NONE,
	/* Installation is in a broken state (version mismatch, missing files). */
	REPENTOGON_INSTALLATION_STATUS_BROKEN,
	/* Installation with a legacy dsound.dll. This value can only be used
	 * if ZHL and Repentogon have the same version AND there is a dsound.dll
	 * found. */
	 REPENTOGON_INSTALLATION_STATUS_LEGACY,
	 /* Installation without a legacy dsound.dll. */
	 REPENTOGON_INSTALLATION_STATUS_MODERN
};

namespace Launcher {
	class Installation;
}

class RepentogonInstallation {
public:
	static constexpr const char* RepentogonSubfolder = "Repentogon";
	static constexpr const char* RepentogonMarker = ".repentogon";

    RepentogonInstallation(ILoggableGUI* gui);

    bool Validate(std::string const& installationPath);
	bool CheckHalfAssedPatch(std::string const& installationPath);
	bool CheckExeFuckMethod(std::string const& installationPath);

	inline RepentogonInstallationStatus GetState() const {
		return _installationState;
	}

	inline LoadDLLState GetDLLLoadState(LoadableDlls dll) const {
		return _dllStates[dll];
	}

	inline std::vector<FoundFile> const& GetRepentogonInstallationFilesState() const {
		return _repentogonFiles;
	}

	inline std::string const& GetRepentogonVersion() const {
		return _repentogonVersion;
	}

	inline std::string const& GetZHLVersion() const {
		return _zhlVersion;
	}

	inline std::string const& GetZHLLoaderVersion() const {
		return _zhlLoaderVersion;
	}

	inline bool RepentogonZHLVersionMatch() const {
		return _repentogonZHLVersionMatch;
	}

	inline bool IsValid() const {
		return _installationState == REPENTOGON_INSTALLATION_STATUS_MODERN;
	}

	inline std::string const& GetShadowZHLVersion() const {
		return _sZhlVersion;
	}

	inline std::string const& GetShadowZHLLoaderVersion() const {
		return _sZhlLoaderVersion;
	}

	inline std::string const& GetShadowRepentogonVersion() const {
		return _sRepentogonVersion;
	}

	static bool IsIsaacVersionCompatible(const char* version);

	friend class Launcher::Installation;
private:
	void Invalidate();

	/* Load a library that has an entry in the _dllStates array.
     *
	 * Returns the result of loading the library. If the load is successful,
	 * the entry in _dllStates is updated to true.
	 */
	HMODULE LoadLib(const char* name, LoadableDlls dll);

	/* DLL management related function: loading core ZHL DLLs, retrieving
	 * symbols and validating that symbols to string are well formed.
	 */
	ScopedModule LoadModule(const char* shortName, const char* path, LoadableDlls dll);
	FARPROC RetrieveSymbol(HMODULE module, const char* libname, const char* symbol);
	bool ValidateVersionSymbol(HMODULE module, const char* libname, const char* symbolName,
		FARPROC versionSymbol, std::string& target, std::string& shadowTarget);

	void ClearInstallation();

    ILoggableGUI* _gui;

	RepentogonInstallationStatus _installationState = REPENTOGON_INSTALLATION_STATUS_NONE;
	std::string _repentogonVersion;
	/* Version of ZHL that is installed. Empty if the info cannot be found. */
	std::string _zhlVersion;
	/* Version of the ZHL loader that is installed. Empty if the info
	 * cannot be found.
	 */
	std::string _zhlLoaderVersion;
	/* Whether the versions of ZHL, the ZHL loader and Repentogon match. False by default. */
	bool _repentogonZHLVersionMatch = false;
	/* SHA-256 hash of the zhlRepentogon.dll found on the disk. NULL if the
	 * info cannot be found.
	 */
	std::unique_ptr<char[]> _dllHash;
	/* List of the mandatory files of a Repentogon installation, and whether
	 * they were found or not.
	 */
	std::vector<FoundFile> _repentogonFiles;
	/* For all DLLs that need to be loaded to retrieve data, indicate whether
	 * the load was successful or not.
	 */
	LoadDLLState _dllStates[LOADABLE_DLL_MAX] = { LOAD_DLL_STATE_NONE };

	/* Shadow variants of the version of the different components.
	 * Used to track version changes during updates.
	 */
	std::string _sRepentogonVersion;
	std::string _sZhlVersion;
	std::string _sZhlLoaderVersion;
};