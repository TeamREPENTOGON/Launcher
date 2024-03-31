#pragma once

#include <Windows.h>

#include <chrono>
#include <string>
#include <vector>

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"

#include "launcher/scoped_file.h"

namespace IsaacLauncher {
	class MainFrame;

	static constexpr const char* version = "alpha";
	static constexpr const char* isaacExecutable = "isaac-ng.exe";

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

	/* Phases in the update of Repentogon. */
	enum RepentogonUpdatePhase {
		/* Checking the assets available. */
		REPENTOGON_UPDATE_PHASE_CHECK_ASSETS,
		/* Downloading. */
		REPENTOGON_UPDATE_PHASE_DOWNLOAD,
		/* Check that the hash of the downloaded file is okay. */
		REPENTOGON_UPDATE_PHASE_CHECK_HASH,
		/* Unpack the downloaded file. */
		REPENTOGON_UPDATE_PHASE_UNPACK,
		/* Done. */
		REPENTOGON_UPDATE_PHASE_DONE
	};

	/* Structure holding the state of the current Repentogon update. 
	 * This is used for logging and debugging purposes.
	 */
	struct RepentogonUpdateState {
		RepentogonUpdateState();
		RepentogonUpdatePhase phase = REPENTOGON_UPDATE_PHASE_CHECK_ASSETS;
		bool zipOk = false; /* zip is present in assets. */
		bool hashOk = false; /* hash is present in assets. */
		std::string hashUrl; /* URL to download the hash. */
		std::string zipUrl; /* URL to download the archive. */
		ScopedFile hashFile; /* Pointer to the file in which we write the hash. */
		ScopedFile zipFile; /* Pointer to the file in which we write the zip. */
		std::string hash; /* Expected hash of the zip file. */
		std::string zipHash; /* Hash of the zip file. */
		/* All files that have been found in the archive and whether they 
		 * were properly unpacked. */
		std::vector<std::tuple<std::string, bool>> unzipedFiles;

		void Clear();
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

	enum RepentogonUpdateResult {
		/* Update was successful. */
		REPENTOGON_UPDATE_RESULT_OK,
		/* Update failed: missing asset. */
		REPENTOGON_UPDATE_RESULT_MISSING_ASSET,
		/* Update filed: curl error during download. */
		REPENTOGON_UPDATE_RESULT_DOWNLOAD_ERROR,
		/* Update failed: hash of REPENTOGON.zip is invalid. */
		REPENTOGON_UPDATE_RESULT_BAD_HASH,
		/* Update failed: error during extraction. */
		REPENTOGON_UPDATE_RESULT_EXTRACT_FAILED
	};

	/* Result of fetching updates. */
	enum FetchUpdatesResult {
		/* Fetched successfully. */
		FETCH_UPDATE_OK,
		/* cURL error. */
		FETCH_UPDATE_BAD_CURL,
		/* Invalid URL. */
		FETCH_UPDATE_BAD_REQUEST,
		/* Download error. */
		FETCH_UPDATE_BAD_RESPONSE,
		/* Response has no "name" field. */
		FETCH_UPDATE_NO_NAME
	};

	/* Result of comparing installed version with latest release. */
	enum VersionCheckResult {
		/* New version available. */
		VERSION_CHECK_NEW,
		/* Up-to-date. */
		VERSION_CHECK_UTD,
		/* Error while checking version. */
		VERSION_CHECK_ERROR
	};

	/* Global array of all known versions of the game. */
	extern Version const knownVersions[];

	/* Helper class used to perform all the tasks related to updating.
	 *
	 * This class exposes methods to check the sanity of the filesystem, i.e. check
	 * the presence of the appropriate files, check their versions wrt expected
	 * versions.
	 *
	 * Many functions return error codes that can be used to get extended error
	 * information. This allows the class to be used outside the context of a GUI.
	 */
	class Updater {
	public:
		Updater();

		/* Check that a file with the given name exists. Return true on success,
		 * false on failure.
		 */
		static bool FileExists(const char* filename);

		/* Check that a file with the given name exists. Fill the search
		 * structure with the result of the search. 
		 * 
		 * Return true on success, false on failure.
		 */
		static bool FileExists(const char* filename, WIN32_FIND_DATAA* search);

		/* Return a pointer to the Version object associated with a version's hash.
		 * If no such hash exists, return NULL.
		 */
		static Version const* GetIsaacVersionFromHash(const char* hash);

		/* Return the SHA-256 hash of the content of filename. 
		 * 
		 * If the function cannot load the content of the file in memory, or if
		 * the file does not exist, the function throws.
		 */
		static std::string Sha256F(const char* filename) noexcept(false);

		/* Return the SHA-256 hash of the string. NULL strings have an empty 
		 * hash. */
		static std::string Sha256(const char* string, size_t size);

		/* Check that the installation of Isaac is valid. 
		 * 
		 * An installation of Isaac is valid if the following conditions are 
		 * met:
		 *	- The current folder contains a file called isaac-ng.exe.
		 * 
		 * After a call to this function, GetIsaacVersion() can be used to 
		 * query information about the currently installed version.
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

		/* Check if a Repentogon update is available. 
		 * 
		 * An update is available if the hash of any of the DLLs is different
		 * from its hash in the latest available release.
		 * 
		 * The response parameter is updated to store the possible answer to
		 * the request.
		 */
		VersionCheckResult CheckRepentogonUpdates(rapidjson::Document& response);

		/* Same as CheckRepentogonUpdates(), except for the launcher. */
		VersionCheckResult CheckLauncherUpdates(rapidjson::Document& response);

		/* Update the installation of Repentogon. 
		 * 
		 * The update is performed by downloading the latest release from 
		 * GitHub, checking that the hash of REPENTOGON.zip matches the content
		 * of hash.txt and then unpacking REPENTOGON.zip into the current 
		 * folder.
		 * 
		 * The base parameter should be the result of querying the latest 
		 * release, as provided by a successful call to CheckRepentogonUpdates().
		 * 
		 * The function returns REPENTOGON_UPDATE_RESULT_OK if the entire update
		 * process was successful, a REPENTOGON_UPDATE_RESULT_* constant 
		 * otherwise.
		 */
		RepentogonUpdateResult UpdateRepentogon(rapidjson::Document& base);

		RepentogonInstallationState GetRepentogonInstallationState() const;
		Version const* GetIsaacVersion() const;
		bool WasLibraryLoaded(LoadableDlls dll) const;
		std::vector<FoundFile> const& GetRepentogonInstallationFilesState() const;
		std::string const& GetRepentogonVersion() const;
		std::string const& GetZHLVersion() const;
		bool RepentogonZHLVersionMatch() const;
		RepentogonUpdateState const& GetRepentogonUpdateState() const;

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
		/* State of the current Repentogon update. */
		RepentogonUpdateState _repentogonUpdateState;

		/* Load a library that has an entry in the _dllStates array.
		 * 
		 * Returns the result of loading the library. If the load is successful,
		 * the entry in _dllStates is updated to true.
		 */
		HMODULE LoadLib(const char* name, LoadableDlls dll);

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

		/* Check that the latest release of Repentogon contains a hash and 
		 * the archive itself.
		 * 
		 * Return true if the release is well-formed, false otherwise.
		 */
		bool CheckRepentogonAssets(rapidjson::Document const& release);

		/* Download the latest release of Repentogon: hash and archive. 
		 * 
		 * Return true if the download completes without issue, false otherwise.
		 */
		bool DownloadRepentogon();

		/* Check that the hash of the downloaded archive matches the hash of
		 * the release.
		 * 
		 * Return true if the hash matches, false otherwise.
		 */
		bool CheckRepentogonIntegrity();

		/* Extract the content of the Repentogon archive. 
		 * 
		 * Return true if the extraction is successful, false otherwise.
		 */
		bool ExtractRepentogon();

		/* Fetch the JSON of the latest Repentogon release. */
		FetchUpdatesResult FetchRepentogonUpdates(rapidjson::Document& result);
	};
}