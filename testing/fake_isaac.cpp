#include <cstdio>
#include <ctime>

int main(int argc, char** argv) {
	FILE* f = fopen("isaac.log", "w");
	time_t now = time(nullptr);
	tm* nowtm = localtime(&now);
	char buffer[4096];
	strftime(buffer, 4096, "[%Y-%m-%d %H:%M:%S] ", nowtm);
	fprintf(f, "%s Launched Isaac\n", buffer);
	for (int i = 1; i < argc; ++i) {
		fprintf(f, "argv[%d] = %s\n", i, argv[i]);
	}
	fclose(f);
	return 0;
}