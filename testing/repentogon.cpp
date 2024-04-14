#include <cstdio>
#include <ctime>

extern "C" {
	__declspec(dllexport) const char* __REPENTOGON_VERSION = "1.0.0";

	__declspec(dllexport) int ModInit() {
		FILE* f = fopen("repentogon.log", "w");
		time_t now = time(nullptr);
		tm* nowtm = localtime(&now);
		char buffer[4096];
		strftime(buffer, 4096, "[%Y-%m-%d %H:%M:%S] ", nowtm);
		fprintf(f, "%s Loaded Repentogon\n", buffer);
		fclose(f);
		return 0;
	}
}