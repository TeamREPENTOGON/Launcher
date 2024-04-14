#include <cstdio>
#include <ctime>

extern "C" {
	__declspec(dllexport) int ModInit() {
		FILE* f = fopen("dummy.log", "w");
		time_t now = time(nullptr);
		tm* nowtm = localtime(&now);
		char buffer[4096];
		strftime(buffer, 4096, "[%Y-%m-%d %H:%M:%S] ", nowtm);
		fprintf(f, "%s Loaded Dummy\n", buffer);
		fclose(f);
		return 0;
	}
}