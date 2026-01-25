#pragma once

#include <Windows.h>

namespace Updater {
	// Simple RAII wrapper for a win32 window.
	class UniqueWindow {
	public:
		UniqueWindow(HWND mainWindow, PCSTR lpClassName, LPCSTR lpWindowName,
			DWORD dwStyle, int x, int y, int w, int h, HINSTANCE hInstance);

		~UniqueWindow();

		operator HWND () const { return _window; }
		operator bool() const { return _window != NULL; }

		HWND GetHandle();
	private:
		HWND _window = NULL;
	};
}