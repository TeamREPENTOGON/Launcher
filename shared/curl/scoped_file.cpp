#include "shared/scoped_file.h"

ScopedFile::ScopedFile() : ScopedFile(nullptr) {

}

ScopedFile::ScopedFile(FILE* f) : _f(f) {

}

ScopedFile::~ScopedFile() {
	if (_f)
		fclose(_f);
}

FILE* ScopedFile::Release() {
	FILE* f = _f;
	_f = nullptr;
	return f;
}

ScopedFile& ScopedFile::operator=(FILE* f) {
	if (_f)
		fclose(_f);

	_f = f;
	return *this;
}

ScopedFile::operator FILE* () const {
	return _f;
}

ScopedFile::operator bool() const {
	return _f != nullptr;
}