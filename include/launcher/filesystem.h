#pragma once

#include <Windows.h>

#include <memory>
#include <string>
#include <vector>

#include "inih/cpp/INIReader.h"

/* filesystem.cpp
 *
 * Various utilities useful to perform checks on the filesystem, such as
 * checking an installation of Isaac / Repentogon, or hashing files.
 */
namespace Launcher {
	namespace fs {
		static constexpr const char* isaacExecutable = "isaac-ng.exe";
		static constexpr const char* launcherConfigFile = "repentogon_launcher.ini";
		static constexpr const char* defaultRepentogonFolder = "repentogon";

		struct Version {
			/* Hash of a given version. */
			const char* hash;
			/* User-friendly string associated with this version. */
			const char* version;
			/* Whether the version is compatible with Repentogon or not. */
			bool valid;
		};

		enum LoadableDlls {
			LOADABLE_DLL_ZHL,
			LOADABLE_DLL_REPENTOGON,
			LOADABLE_DLL_MAX
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

		enum IsaacInstallationPathInitResult {
			INSTALLATION_PATH_INIT_OK,
			/* Error while retrieving the profile directory. */
			INSTALLATION_PATH_INIT_ERR_PROFILE_DIR,
			/* Error: the save folder does not exist. */
			INSTALLATION_PATH_INIT_ERR_NO_SAVE_DIR,
			/* Error: the configuration file does not exist. */
			INSTALLATION_PATH_INIT_ERR_NO_CONFIG,
			/* Error: can't open configuration file. */
			INSTALLATION_PATH_INIT_ERR_OPEN_CONFIG,
			/* Error: can't parse configuration file. */
			INSTALLATION_PATH_INIT_ERR_PARSE_CONFIG,
			/* Error: configuration file does not point towards an Isaac folder, 
			 * and no isaac-ng.exe found in current folder. 
			 */
			INSTALLATION_PATH_INIT_ERR_NO_ISAAC,
			/* Error: configuration file does not point towards a valid Isaac folder. */
			INSTALLATION_PATH_INIT_ERR_BAD_ISAAC_FOLDER,
			/* Error: configuration file does not point towards a valid Repentogon folder. */
			INSTALLATION_PATH_INIT_ERR_BAD_REPENTOGON_FOLDER,
			/* Error: malformed configuration file. */
			INSTALLATION_PATH_INIT_ERR_MALFORMED_CONFIG
		};

		enum ConfigurationFileLocation {
			CONFIG_FILE_LOCATION_HERE,
			CONFIG_FILE_LOCATION_SAVE,
		};

		/* Global array of all known versions of the game. */
		extern Version const knownVersions[];

		/* Check if a folder with the given name exists. Return true on success, 
		 * false on failure. 
		 */
		bool FolderExists(const char* name);

		/* Check that a file with the given name exists. Return true on success,
		 * false on failure.
		 */
		bool FileExists(const char* filename);

		/* Check that a file with the given name exists. Fill the search
		 * structure with the result of the search.
		 *
		 * Return true on success, false on failure.
		 */
		bool FileExists(const char* filename, WIN32_FIND_DATAA* search);

		std::string GetCurrentDirectory_();

		/* Return a pointer to the Version object associated with a version's hash.
		 * If no such hash exists, return NULL.
		 */
		Version const* GetIsaacVersionFromHash(const char* hash);

		/* Return the SHA-256 hash of the content of filename.
		 *
		 * If the function cannot load the content of the file in memory, or if
		 * the file does not exist, the function throws.
		 */
		std::string Sha256F(const char* filename) noexcept(false);

		/* Return the SHA-256 hash of the string. NULL strings have an empty
		 * hash. */
		std::string Sha256(const char* string, size_t size);

		class Installation {
		public:
			/* Initialize the save folder, Isaac installation, launcher 
			 * configuration and Repentogon installation (if any) paths.
			 * 
			 * If the function succeeds, the functions GetIsaacInstallationFolder(),
			 * GetLauncherConfigurationFolder() and GetRepentogonInstallationFolder()
			 * return the found values.
			 * 
			 * If the function fails, the aforementioned functions can work 
			 * depending on where the failure occured (an error while searching
			 * the Isaac folder still allows GetLauncherConfigurationFolder()
			 * to work).
			 */
			IsaacInstallationPathInitResult InitFolders();

			/* Check that the installation of Isaac is valid.
			 *
			 * An installation of Isaac is valid if _isaacFolder contains a file 
			 * called isaac-ng.exe.
			 *
			 * After a call to this function, GetIsaacVersion() can be used to
			 * query information about the currently installed version, if 
			 * an installation was found.
			 */
			bool CheckIsaacInstallation();

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

			void SetLauncherConfigurationPath(ConfigurationFileLocation loc);
			bool SetIsaacInstallationFolder(std::string const& folder);

			RepentogonInstallationState GetRepentogonInstallationState() const;
			Version const* GetIsaacVersion() const;
			bool WasLibraryLoaded(LoadableDlls dll) const;
			std::vector<FoundFile> const& GetRepentogonInstallationFilesState() const;
			std::string const& GetRepentogonVersion() const;
			std::string const& GetZHLVersion() const;
			bool RepentogonZHLVersionMatch() const;

			std::string const& GetSaveFolder() const;
			std::string const& GetLauncherConfigurationPath() const;
			std::string const& GetIsaacInstallationFolder() const;
			std::string const& GetRepentogonInstallationFolder() const;

			int GetConfigurationFileSyntaxErrorLine() const;

		private:
			/* State of the Repentogon installation. */
			RepentogonInstallationState _installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			/* Version of Repentogon that is installed. Empty if the info cannot
			 * be found.
			 */
			std::string _repentogonVersion;
			/* Version of ZHL that is installed. Empty if the info cannot be found. */
			std::string _zhlVersion;
			/* Whether the versions of ZHL and Repentogon match. False by default. */
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
			bool _dllStates[LOADABLE_DLL_MAX] = { false };
			/* Path to the folder containing the Repentance save file. */
			std::string _saveFolder;
			/* Path to the folder containing the configuration of the launcher. */
			std::string _configurationPath;
			/* Path to the Isaac installation. */
			std::string _isaacFolder;
			/* Path to the folder containing the current installation of 
			 * Repentogon, if any. 
			 */
			std::string _repentogonFolder;
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

			/* Find the folder containing the Repentance save files. 
			 * 
			 * The function will look in the user's profile dir, as does the
			 * game.
			 * 
			 * This function can fail.
			 */
			IsaacInstallationPathInitResult SearchSaveFolder();

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
			IsaacInstallationPathInitResult SearchConfigurationFile();

			/* Find the folder containing the installation of Isaac. 
			 * 
			 * The function first checks the content of the configuration file.
			 * If the config indicates an installation folder, the function 
			 * checks that it is an existing folder, but doesn't check the 
			 * validity of the installation (see CheckIsaacInstallation()). 
			 * 
			 * If the configuration file does not indicate an installation 
			 * folder, the function checks for a file called isaac-ng.exe in
			 * the current folder. If such a file is found, the current folder
			 * becomes the Isaac installation folder.
			 * 
			 * This function can fail.
			 */
			IsaacInstallationPathInitResult SearchIsaacInstallationPath();

			/* Find the folder containing the installation of Repentogon.
			 * 
			 * The function first checks the content of the configuration file.
			 * If the config indicates an installation folder, the function
			 * checks that it is an existing folder, but doesn't check the
			 * validaity of the installation (see CheckRepentogonInstallation()).
			 * 
			 * If the configuration file does not indicate an installation
			 * folder, the function checks for a folder called repentogon in 
			 * the current folder. If such a folder is found, it becomes the 
			 * Repentogon installation folder (no validation is performed).
			 * 
			 * This function can fail.
			 */
			IsaacInstallationPathInitResult SearchRepentogonInstallationPath();

			/* Open and parse the configuration file pointed by path.
			 * 
			 * This function can fail.
			 */
			IsaacInstallationPathInitResult ProcessConfigurationFile(std::string const& path);
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
}