#include "shared/scoped_module.h"

ScopedModule::ScopedModule() {

}

ScopedModule::ScopedModule(ScopedModule&& other) : _module(other._module) {
	other._module = NULL;
}

ScopedModule::ScopedModule(HMODULE mod) : _module(mod) {

}

ScopedModule& ScopedModule::operator=(ScopedModule&& other) {
	_module = other._module;
	other._module = NULL;
	return *this;
}

ScopedModule::~ScopedModule() {
	if (_module) {
		FreeLibrary(_module);
	}
}

HMODULE ScopedModule::Get() const {
	return _module;
}

ScopedModule::operator bool() const {
	return _module != NULL;
}

ScopedModule::operator HMODULE() const {
	return _module;
}