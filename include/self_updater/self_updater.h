#pragma once

namespace Updater {
	[[noreturn]] void StartLauncher();

	/* Name of the CLI flag that causes the lock file to be ignored. */
	extern const char* ForcedArg;
	/* Name of the CLI flag that allows updating to unstable versions. No effect if a specific URL is provided. */
	extern const char* UnstableArg;
	/* Name of the CLI flag for passing a specific URL to download an update from, instead of fetching the latest releases. */
	extern const char* UrlArg;
}