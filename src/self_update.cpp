#include "launcher/self_updater/launcher_updater.h"
#include "unpacker/unpacker_resources.h"

#include "comm/messages.h"
#include "launcher/launcher.h"
#include "launcher/self_update.h"
#include "launcher/version.h"
#include "rapidjson/document.h"
#include "shared/github.h"
#include "shared/logger.h"

namespace Launcher {
	static Github::DownloadAsStringResult FetchReleases(rapidjson::Document& answer);

	static const char* ReleasesURL = "https://api.github.com/repos/TeamREPENTOGON/Launcher/releases";

	Github::DownloadAsStringResult FetchReleases(rapidjson::Document& answer) {
		Threading::Monitor<Github::GithubDownloadNotification> monitor;
		std::string releaseString;
		// return Github::FetchReleaseInfo(ReleasesURL, answer, &monitor);
		Github::DownloadAsStringResult result = Github::DownloadAsString(ReleasesURL, releaseString, nullptr);
		if (result != Github::DOWNLOAD_AS_STRING_OK) {
			return result;
		}

		answer.Parse(releaseString.c_str());
		if (answer.HasParseError()) {
			return Github::DOWNLOAD_AS_STRING_INVALID_JSON;
		}

		return Github::DOWNLOAD_AS_STRING_OK;
	}

	bool SelectTargetRelease(rapidjson::Document const& releases, bool allowPre,
		bool force, std::string& version, std::string& url);

	bool SelfUpdater::IsSelfUpdateAvailable(bool allowDrafts, bool force, 
		std::string& version, std::string& url, Github::DownloadAsStringResult* fetchReleasesResult) {
		Github::DownloadAsStringResult downloadResult = FetchReleases(_releasesInfo);
		if (fetchReleasesResult)
			*fetchReleasesResult = downloadResult;

		if (downloadResult != Github::DOWNLOAD_AS_STRING_OK) {
			return false;
		}

		rapidjson::Document& releases = _releasesInfo;
		_hasRelease = true;
		
		return SelectTargetRelease(releases, allowDrafts, force, version, url);
	}

	bool SelectTargetRelease(rapidjson::Document const& releases, bool allowPre,
		bool force, std::string& version, std::string& url) {
		auto releaseArray = releases.GetArray();
		for (auto const& release : releaseArray) {
			if (!release["prerelease"].IsTrue() || allowPre) {
				version = release["name"].GetString();
				url = release["url"].GetString();
				return true;
			}
		}

		return false;
	}

	SelfUpdateErrorCode SelfUpdater::SelectReleaseTarget(bool allowPreRelease, bool force) {
		SelfUpdateErrorCode result;
		rapidjson::Document releases;

		Github::DownloadAsStringResult releasesResult = FetchReleases(releases);
		if (releasesResult != Github::DOWNLOAD_AS_STRING_OK) {
			result.base = SELF_UPDATE_UPDATE_CHECK_FAILED;
			result.detail = releasesResult;
			return result;
		}

		std::string version, url;
		if (!SelectTargetRelease(releases, allowPreRelease, force, version, url)) {
			result.base = SELF_UPDATE_UP_TO_DATE;
			return result;
		}

		result.base = SELF_UPDATE_CANDIDATE;
		result.detail = CandidateVersion{ .version = version, .url = url };
		return result;
	}
}