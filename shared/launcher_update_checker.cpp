#include "comm/messages.h"
#include "launcher/version.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"

#include "shared/github.h"
#include "shared/launcher_update_checker.h"
#include "shared/logger.h"
#include <shared/gitlab_versionchecker.h>
#include <regex>

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


	//this is a copypaste, should be on a shared H between launcher and updater but Im lazy
	struct Version {
		std::vector<int> numbers;
		bool isBeta = false;
		bool isUnstable = false;
		bool isDev = false;
	};

	Version parseVersion(const std::string& s) {
		Version v;

		if (s.empty())
			return v;

		// Make a copy so we can manipulate it
		std::string str = s;

		// Normalize to lowercase
		std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });

		if (str == "dev") {
			v.isDev = true;
			return v;
		}

		// Trim leading v
		if (str[0] == 'v')
			str.erase(0, 1);

		// Split into tokens
		std::regex re(R"([.\-_]+)");
		std::sregex_token_iterator first{ str.begin(), str.end(), re, -1 }, last;
		std::vector<std::string> tokens{ first, last };

		for (const std::string& token : tokens) {
			if (token == "beta") {
				v.isBeta = true;
			}
			else if (token == "unstable") {
				v.isUnstable = true;
			}
			else {
				try {
					v.numbers.push_back(std::stoi(token));
				}
				catch (...) {
					Logger::Error("RepentogonUpdater::parseVersion: Unrecognized token `%s` found in version string `%s`\n", token.c_str(), s.c_str());
				}
			}
		}

		return v;
	}

	int compareVersions(const std::string& a, const std::string& b) {
		Version va = parseVersion(a);
		Version vb = parseVersion(b);

		// "dev" > everything else
		if (va.isDev || vb.isDev) {
			if (va.isDev && vb.isDev) {
				return 0;
			}
			return va.isDev ? 1 : -1;
		}

		size_t maxParts = std::max(va.numbers.size(), vb.numbers.size());
		for (size_t i = 0; i < maxParts; ++i) {
			int na = (i < va.numbers.size()) ? va.numbers[i] : 0;
			int nb = (i < vb.numbers.size()) ? vb.numbers[i] : 0;

			if (na < nb) return -1;
			if (na > nb) return 1;
		}

		//same numbers then release > beta > unstable
		if (va.isUnstable != vb.isUnstable) {
			return va.isUnstable ? -1 : 1;
		}
		if (va.isBeta != vb.isBeta) {
			return va.isBeta ? -1 : 1;
		}

		return 0;
	}
	//this is a copypaste Im lazy end

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
			if (!std::filesystem::exists("steamentrydir.txt"))
				return false;
			std::ifstream in("steamentrydir.txt");
			std::ostringstream ss;
			ss << in.rdbuf();
			url = ss.str(); //yes, I use url for the local dir, sue me
			if (!std::filesystem::exists(url + "/versiontracker/versionlauncher.txt"))
				return false;
			std::ifstream in2(url + "/versiontracker/versionlauncher.txt");
			std::ostringstream ss2;
			ss2 << in2.rdbuf();
			std::string availableversion = ss2.str();
			std::string currversion = ::Launcher::LAUNCHER_VERSION;
			version = availableversion;
			_hasRelease = true;
			return compareVersions(currversion,availableversion) < 0;
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