#include "launcher/version.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"

#include "shared/github.h"
#include "shared/launcher_update_checker.h"
#include "shared/logger.h"
#include "shared/gitlab_versionchecker.h"
#include "shared/version_utils.h"

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

	bool SelectTargetRelease(rapidjson::Document const& releases, bool allowPreRelease,
		bool force, std::string& version, std::string& url);

	bool LauncherUpdateChecker::IsSelfUpdateAvailable(bool allowPreRelease, bool force,
		std::string& version, std::string& url, curl::DownloadStringResult& fetchReleasesResult, SteamLauncherUpdateStatus& steamUpdateStatus) {

		steamUpdateStatus = STEAM_LAUNCHER_UPDATE_NOT_USED;

		//GitLab Barrier
		if (!force && !allowPreRelease && !ShouldDoTimeBasedGitLabSkip()) {
			if (RemoteGitLabVersionMatches("versionlauncher", Launcher::LAUNCHER_VERSION)) {
				fetchReleasesResult = curl::DownloadStringResult::DOWNLOAD_STRING_OK; //I would call this up a level so I dont have to do this hacky, but here is more convenient because I have some things already pre-chewed
				return false;
			}
		}
		//GitLab Barrier END

		bool downloadResult = FetchReleases(&fetchReleasesResult, _releasesInfo);

		if (!downloadResult) {
			// Double check with gitlab before resorting to steam (we may not have checked gitlab earlier).
			if (!allowPreRelease && RemoteGitLabVersionMatches("versionlauncher", Launcher::LAUNCHER_VERSION)) {
				fetchReleasesResult = curl::DownloadStringResult::DOWNLOAD_STRING_OK;
				return false;
			}
			Logger::Warn("Failed to fetch releases from GitHub. Trying Steam...\n");
			if (!std::filesystem::exists("steamentrydir.txt")) {
				Logger::Warn("steamentrydir.txt does not exist. Cannot proceed.\n");
				steamUpdateStatus = STEAM_LAUNCHER_UPDATE_FAILED;
				return false;
			}
			std::ifstream in("steamentrydir.txt");
			std::ostringstream ss;
			ss << in.rdbuf();
			url = ss.str(); //yes, I use url for the local dir, sue me
			if (!std::filesystem::exists(url + "/versiontracker/versionlauncher.txt")) {
				Logger::Error("versionlauncher.txt does not exist. Cannot proceed.\n");
				steamUpdateStatus = STEAM_LAUNCHER_UPDATE_FAILED;
				return false;
			}
			std::ifstream in2(url + "/versiontracker/versionlauncher.txt");
			std::ostringstream ss2;
			ss2 << in2.rdbuf();
			std::string availableversion = ss2.str();
			std::string currversion = ::Launcher::LAUNCHER_VERSION;
			version = availableversion;
			_hasRelease = true;
			Logger::Info("Comparing current version `%s` with steam version `%s`...\n", currversion.c_str(), availableversion.c_str());
			if (LauncherVersion(currversion) < LauncherVersion(availableversion)) {
				Logger::Info("Steam version is newer. Considering it as an available update...\n");
				steamUpdateStatus = STEAM_LAUNCHER_UPDATE_AVAILABLE;
				return true;
			}
			Logger::Info("Could not identify the steam version as being newer. Ignoring.\n");
			steamUpdateStatus = STEAM_LAUNCHER_UPDATE_UP_TO_DATE;
			return false;
		}

		_hasRelease = true;
		return SelectTargetRelease(_releasesInfo, allowPreRelease, force, version, url) && strcmp(::Launcher::LAUNCHER_VERSION, version.c_str());
	}

	bool SelectTargetRelease(rapidjson::Document const& releases, bool allowPreRelease,
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
			if (!release["prerelease"].IsTrue() || allowPreRelease) {
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