#pragma once

#include <cstdint>
#include <variant>

#include "shared/github.h"

namespace Launcher {
	extern const char* SelfUpdaterExePath;

	enum SelfUpdateExtractionResult {
		SELF_UPDATE_EXTRACTION_OK,
		SELF_UPDATE_EXTRACTION_ERR_RESOURCE_NOT_FOUND,
		SELF_UPDATE_EXTRACTION_ERR_RESOURCE_LOAD_FAILED,
		SELF_UPDATE_EXTRACTION_ERR_BAD_RESOURCE_SIZE,
		SELF_UPDATE_EXTRACTION_ERR_RESOURCE_LOCK_FAILED,
		SELF_UPDATE_EXTRACTION_ERR_OPEN_TEMPORARY_FILE,
		SELF_UPDATE_EXTRACTION_ERR_WRITTEN_SIZE
	};

	/* No OK result as the process terminates if everything goes well. */
	enum SelfUpdateRunUpdaterResult {
		SELF_UPDATE_RUN_UPDATER_ERR_OPEN_LOCK_FILE,
		SELF_UPDATE_RUN_UPDATER_ERR_GENERATE_CLI,
		SELF_UPDATE_RUN_UPDATER_ERR_CREATE_PROCESS,
		SELF_UPDATE_RUN_UPDATER_ERR_OPEN_PROCESS,
		SELF_UPDATE_RUN_UPDATER_ERR_WAIT_TIMEOUT,
		SELF_UPDATE_RUN_UPDATER_ERR_WAIT
	};

	/* No OK result as the process terminates if everything goes well. */
	enum SelfUpdateResult {
		/* Everything up-to-date. */
		SELF_UPDATE_UP_TO_DATE,
		/* General error when checking for updates. */
		SELF_UPDATE_UPDATE_CHECK_FAILED,
		/* General error when extracting the self updater. */
		SELF_UPDATE_EXTRACTION_FAILED,
		/* General error when running the self updater. */
		SELF_UPDATE_SELF_UPDATE_FAILED
	};

	struct SelfUpdateErrorCode {
		SelfUpdateResult base : 1;
		union {
			Github::DownloadAsStringResult fetchUpdatesResult : 8;
			SelfUpdateExtractionResult extractionResult : 8;
			SelfUpdateRunUpdaterResult runUpdateResult : 8;
		} detail;
	};

	SelfUpdateErrorCode DoSelfUpdate(bool allowDrafts, bool resume, bool force);
}