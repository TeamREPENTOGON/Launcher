#pragma once

#include <cstdio>

/* RAII-style class that Automatically closes a FILE object upon destruction. */
class ScopedFile {
public:
	ScopedFile();
	ScopedFile(FILE* f);
	~ScopedFile();

	FILE* Release();
	ScopedFile& operator=(FILE* f);
	operator FILE* () const;
	operator bool() const;

private:
	FILE* _f = NULL;
};