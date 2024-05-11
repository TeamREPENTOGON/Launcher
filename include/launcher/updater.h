#pragma once

#include <Windows.h>

#include <chrono>
#include <string>
#include <vector>

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"

#include "shared/scoped_file.h"

namespace Launcher {
	class MainFrame;

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

		/* Check if a Repentogon update is available. 
		 * 
		 * An update is available if the hash of any of the DLLs is different
		 * from its hash in the latest available release.
		 * 
		 * The response parameter is updated to store the possible answer to
		 * the request.
		 */
		VersionCheckResult CheckRepentogonUpdates(rapidjson::Document& response, 
			fs::Installation const& installation);

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

		RepentogonUpdateState const& GetRepentogonUpdateState() const;

	private:
		/* State of the current Repentogon update. */
		RepentogonUpdateState _repentogonUpdateState;

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