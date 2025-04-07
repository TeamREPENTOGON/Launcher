#pragma once

#include "self_updater/unique_window.h"

#include <Windows.h>

#include "self_updater/logger.h"

namespace Updater {
	UniqueWindow::UniqueWindow(LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int x, int y, int w, int h, HINSTANCE hInstance) {
		_window = CreateWindowExA(0, lpClassName, lpWindowName, dwStyle, x, y, w, h, NULL, NULL, hInstance, NULL);
		if (_window == NULL) {
			Logger::Error("Error while creating window (%d)\n", GetLastError());
		}
	}

	UniqueWindow::~UniqueWindow() {
		if (_window != NULL && !DestroyWindow(_window)) {
			Logger::Error("Error while destroying window (%d)\n", GetLastError());
		}
	}

	HWND UniqueWindow::GetHandle() {
		return _window;
	}
}