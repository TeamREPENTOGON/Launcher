#pragma once

#include "wx/wx.h"

namespace Updater {
	enum UpdateState {
		/* Launcher has created the updater, and is ready to be updated. */
		UPDATE_STATE_INIT,
	};

	class App : public wxApp {
	public:
		bool OnInit() override;
	};

	class Updater : public wxFrame {
	public:
		Updater();
	};
}