#include "comm/messages.h"
#include "launcher/version.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"

#include "shared/github.h"
#include "shared/launcher_update_checker.h"
#include "shared/logger.h"
#include <shared/gitlab_versionchecker.h>

namespace Shared {
	static bool FetchReleases(curl::DownloadStringResult* curlResult,
		rapidjson::Document& answer);

	static const char* ReleasesURL = "https://api.github.com/repos/TeamREPENTOGON/Launcher/releases";

	bool FetchReleases(curl::DownloadStringResult* curlResult,
		rapidjson::Document& answer) {
		curl::RequestParameters request;
		request.maxSpeed = request.serverTimeout = request.timeout = 0;
		request.url = ReleasesURL;
		Github::GenerateGithubHeaders(request);

		curl::DownloadStringDescriptor result = curl::DownloadString(request, "launcher releases info");
		if (curlResult) {
			*curlResult = result.result;
		}

		if (result.result != curl::DOWNLOAD_STRING_OK) {
			return false;
		}

		rapidjson::Document response;
		response.Parse(result.string.c_str());

		if (response.HasParseError()) {
			return false;
		}

		answer = std::move(response);
		return true;
	}

	bool SelectTargetRelease(rapidjson::Document const& releases, bool allowPre,
		bool force, std::string& version, std::string& url);

	bool LauncherUpdateChecker::IsSelfUpdateAvailable(bool allowDrafts, bool force,
		std::string& version, std::string& url, curl::DownloadStringResult* fetchReleasesResult) {

		//GitLab Barrier
		if (!force && !ShouldDoTimeBasedGitLabSkip()) {
			if (RemoteGitLabVersionMatches("versionlauncher", Launcher::LAUNCHER_VERSION)) {
				*fetchReleasesResult = curl::DownloadStringResult::DOWNLOAD_STRING_OK; //I would call this up a level so I dont have to do this hacky, but here is more convenient because I have some things already pre-chewed
				return false;
			}
		}
		//GitLab Barrier END

		bool downloadResult = FetchReleases(fetchReleasesResult, _releasesInfo);

		if (!downloadResult) {
			return false;
		}

		_hasRelease = true;
		return SelectTargetRelease(_releasesInfo, allowDrafts, force, version, url) && strcmp(::Launcher::LAUNCHER_VERSION, version.c_str());
	}

	bool SelectTargetRelease(rapidjson::Document const& releases, bool allowPre,
		bool, std::string& version, std::string& url) {
		if (!releases.IsArray()) {
			rapidjson::StringBuffer buffer;
			rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
			releases.Accept(writer);
			Logger::Error("SelectTargetRelease: malformed answer (got %s)\n", buffer.GetString());
			return false;
		}

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

	SelfUpdateErrorCode LauncherUpdateChecker::SelectReleaseTarget(bool allowPreRelease, bool force) {
		SelfUpdateErrorCode result;
		rapidjson::Document releases;

		curl::DownloadStringResult releasesResult;
		FetchReleases(&releasesResult, releases);
		if (releasesResult != curl::DOWNLOAD_STRING_OK) {
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