#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include <memory>
#include <string>
#include <vector>

#include "inih/cpp/INIReader.h"
#include "shared/loggable_gui.h"
#include "shared/scoped_module.h"

/* filesystem.cpp
 *
 * Various utilities useful to perform checks on the filesystem, such as
 * checking an installation of Isaac / Repentogon, or hashing files.
 */
namespace Launcher {
	static constexpr const char* launcherConfigFile = "repentogon_launcher.ini";
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

	struct Version {
		/* Hash of a given version. */
		const char* hash;
		/* User-friendly string associated with this version. */
		const char* version;
		/* Whether the version is compatible with Repentogon or not. */
		bool valid;
	};

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
	enum RepentogonInstallationState {
		/* No installation found, or installation in an invalid state. */
		REPENTOGON_INSTALLATION_STATE_NONE,
		/* Installation with a legacy dsound.dll. This value can only be used
		 * if ZHL and Repentogon have the same version AND there is a dsound.dll
		 * found. */
		REPENTOGON_INSTALLATION_STATE_LEGACY,
		/* Installation without a legacy dsound.dll. */
		REPENTOGON_INSTALLATION_STATE_MODERN
	};

	enum ConfigurationFileLocation {
		CONFIG_FILE_LOCATION_HERE,
		CONFIG_FILE_LOCATION_SAVE,
	};

	enum ReadVersionStringResult {
		READ_VERSION_STRING_OK,
		READ_VERSION_STRING_ERR_NO_FILE,
		READ_VERSION_STRING_ERR_OPEN,
		READ_VERSION_STRING_ERR_NO_MEM,
		READ_VERSION_STRING_ERR_TOO_BIG,
		READ_VERSION_STRING_ERR_INVALID_PE,
		READ_VERSION_STRING_ERR_NO_VERSION
	};

	/* Global array of all known versions of the game. */
	extern Version const knownVersions[];

	/* Class representing the state of an installation of Isaac and Repentogon.
	 * 
	 * This class acts as a STATE MACHINE. Attributes are default initialized
	 * to empty values. 
	 * 
	 * The states are the following
	 *	* Empty (default initialized)
	 *  * Isaac save folder found
	 *  * Launcher configuration file found (dependent on Isaac save folder found)
	 *  * Isaac installation found (can be entered from the launcher configuration
	 * file found state, but can be triggered manually).
	 *  * Repentogon installation found (dependent on Isaac installation found)
	 * 
	 * Because states form a complex graph, it is recommended to check for
	 * co-dependent states.
	 */
	class Installation {
	public:
		Installation(ILoggableGUI* gui);

		/* Initialize the save folder, Isaac installation, and launcher 
		 * configuration file paths.
		 * 
		 * The function works in three steps, each step progressing only if the
		 * previous one succeeded: first it searches the save folder, then the 
		 * launcher configuration file, then the Isaac installation.
		 * 
		 * Rationale: the save folder contains the configuration file that should
		 * contain the Isaac installation path. If the save folder does not exist,
		 * there is no configuration to load. If there is no configuration to load,
		 * there is no available path to Isaac.
		 * 
		 * The function returns true if all paths are found, false if any of 
		 * them are missing. The getters for the three different paths will
		 * return empty values for not found / searched paths, existing paths
		 * otherwise.
		 */
		bool InitFolders();

		/* Configure the path to the Isaac executable.
		 * 
		 * The function first checks if the given path is a valid Isaac
		 * executable (i.e. the file exists). If the path is not a valid 
		 * executable file, the function returns false.
		 * 
		 * This function additionally attempts to find a Repentogon installation
		 * in the folder of the executable, if the path is a valid executable.
		 * 
		 * If the path is a valid executable folder, GetIsaacVersion()and 
		 * GetIsaacExecutablePath() will return exploitable values. All 
		 * functions related to an installation of  Repentogon become usable as 
		 * well, but may still return an empty path.
		 */
		bool ConfigureIsaacExecutableFile(std::string const& folder);

		/* Check for an installation of Repentogon.
		 *
		 * The function determines what kind of installation we are dealing
		 * with. After a call to this function, calls to GetRepentogonInstallationState()
		 * and GetRepentogonVersion() will return an accurate value.
		 *
		 * The function returns true if all DLLs required are present and the
		 * versions of ZHL and Repentogon match. Otherwise it returns false.
		 * The presence of dsound.dll does not change the return value and
		 * merely changes the value returned by GetRepentogonInstallationState()
		 * and GetRepentogonVersion().
		 */
		bool CheckRepentogonInstallation();

		bool IsCompatibleWithRepentogon();

		ScopedModule LoadModule(const char* shortName, const char* path, LoadableDlls dll);
		FARPROC RetrieveSymbol(HMODULE module, const char* libname, const char* symbol);
		bool ValidateVersionSymbol(HMODULE module, const char* libname, const char* symbolName, 
			FARPROC versionSymbol, std::string& target);

		void SetLauncherConfigurationPath(ConfigurationFileLocation loc);
		void WriteLauncherConfigurationFile();

		RepentogonInstallationState GetRepentogonInstallationState() const;
		Version const* GetIsaacVersion() const;
		LoadDLLState GetDLLLoadState(LoadableDlls dll) const;
		std::vector<FoundFile> const& GetRepentogonInstallationFilesState() const;
		std::string const& GetRepentogonVersion() const;
		std::string const& GetZHLVersion() const;
		std::string const& GetZHLLoaderVersion() const;
		bool RepentogonZHLVersionMatch() const;

		std::string const& GetSaveFolder() const;
		std::string const& GetLauncherConfigurationPath() const;
		std::string const& GetIsaacExecutablePath() const;
		std::string const& GetIsaacInstallationFolder() const;

		bool IsValidRepentogonInstallation(bool includeLegacy) const;
		bool IsLegacyRepentogonInstallation() const;

		int GetConfigurationFileSyntaxErrorLine() const;

		ReadVersionStringResult GetVersionString(std::string& version) const;

		std::unique_ptr<INIReader> const& GetConfigurationFileParser() const;

	private:
		ILoggableGUI* _gui = NULL;

		/* State of the Repentogon installation. */
		RepentogonInstallationState _installationState = REPENTOGON_INSTALLATION_STATE_NONE;
		/* Version of Repentogon that is installed. Empty if the info cannot
		 * be found.
		 */
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
		/* Version of the current installation of Isaac. NULL if no installation
		 * found, or version unknown.
		 */
		Version const* _isaacVersion = NULL;
		/* List of the mandatory files of a Repentogon installation, and whether
		 * they were found or not.
		 */
		std::vector<FoundFile> _repentogonFiles;
		/* For all DLLs that need to be loaded to retrieve data, indicate whether
		 * the load was successful or not.
		 */
		LoadDLLState _dllStates[LOADABLE_DLL_MAX] = { LOAD_DLL_STATE_NONE };
		/* Path to the folder containing the Repentance save file. */
		std::string _saveFolder;
		/* Path to the folder containing the configuration of the launcher. */
		std::string _configurationPath;
		/* Path to the folder containing the Isaac executable, derived from _isaacExecutable. */
		std::string _isaacFolder;
		/* Path to the Isaac executable, or empty if not found. */
		std::string _isaacExecutable;
		/* Eventual line in the launcher configuration file that resulted
		 * in a parse error. 
		 */
		int _configFileParseErrorLine = 0;
		std::unique_ptr<INIReader> _configurationFile;

		/* Load a library that has an entry in the _dllStates array.
		 *
		 * Returns the result of loading the library. If the load is successful,
		 * the entry in _dllStates is updated to true.
		 */
		HMODULE LoadLib(const char* name, LoadableDlls dll);

		/* Find the folder containing the configuration of the launcher.
		 * 
		 * The function first checks for a file named repentogon_launcher.ini
		 * in the current folder. If this file is not found, it checks for 
		 * that file in the Repentance save folder.
		 * 
		 * If the file is found, the function subsequently parses it.
		 * 
		 * This function can fail.
		 */
		bool SearchConfigurationFile();

		/* Find the BoI executable to run. 
		 * 
		 * The function first checks the content of the configuration file.
		 * If the config indicates an executable file, the function 
		 * checks that it is a valid executable, but doesn't check the 
		 * validity of the installation (see CheckIsaacInstallation()). 
		 * 
		 * If the configuration file does not indicate an executable 
		 * path, the function checks for a file called isaac-ng.exe in
		 * the current folder. If such a file is found, it becomes the path
		 * to the executable to run.
		 * 
		 * This function can fail.
		 */
		bool SearchIsaacInstallationPath();

		/* Open and parse the configuration file pointed by path.
		 * 
		 * This function can fail.
		 */
		bool ProcessConfigurationFile(std::string const& path);

		bool SearchIsaacSaveFolder();

		bool IsCompatibleWithRepentogon(std::string const& version);

		bool CheckIsaacExecutable(std::string const& path);
	};

	/* Validate a string in an unsafe module.
	 *
	 * The string starting at address addr in the module module is valid if
	 * and only if it does not extend past the boundaries of the module.
	 * The function searches for a terminating zero in order to end the
	 * string.
	 *
	 * Return true if the string is valid, false otherwise. If the string
	 * is valid, it is copied inside result.
	 */
	bool ValidateVersionString(HMODULE module, const char** addr, std::string& result);
}