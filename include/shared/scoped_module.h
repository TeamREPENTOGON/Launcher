#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include <cstdio>

/* RAII-style class that automatically unloads a module upon destruction. */
class ScopedModule {
public:
	ScopedModule();
	ScopedModule(ScopedModule&& other);
	ScopedModule(ScopedModule const& other) = delete;

	ScopedModule(HMODULE mod);

	ScopedModule& operator=(ScopedModule&& other);
	ScopedModule& operator=(ScopedModule const&) = delete;

	~ScopedModule();

	HMODULE Get() const;
	operator bool() const;
	operator HMODULE() const;

private:
	HMODULE _module = NULL;
};