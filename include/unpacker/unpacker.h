#pragma once

namespace Unpacker {
	bool ExtractArchive(const char* name);
	[[noreturn]] void StartLauncher();

	/* Name of the CLI flag that indicates a continuation of a previous attempt. */
	extern const char* ResumeArg;
	/* Name of the CLI flag that causes the lock file to be ignored. */
	extern const char* ForcedArg;
}