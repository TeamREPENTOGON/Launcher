#pragma once

#include <Windows.h>

namespace Updater {
	// Simple RAII wrapper for a win32 window.
	class UniqueWindow {
	public:
		UniqueWindow(HWND mainWindow, PCSTR lpClassName, LPCSTR lpWindowName,
			DWORD dwStyle, int x, int y, int w, int h, HINSTANCE hInstance);

		~UniqueWindow();

		HWND GetHandle();
	private:
		HWND _window = NULL;
	};
}