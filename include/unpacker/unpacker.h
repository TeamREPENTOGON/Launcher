#pragma once

namespace Unpacker {
	bool ExtractArchive(const char* name);
	[[noreturn]] void StartLauncher();
}