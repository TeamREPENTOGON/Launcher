#include "self_updater/updater.h"

namespace Updater {
	bool App::OnInit() {
		Updater* frame = new Updater();
		frame->Show();
		return true;
	}

	Updater::Updater() : wxFrame(NULL, -1, "REPENTOGON Launcher Updater") {
		SetSize(1024, 650);
	}
}

wxIMPLEMENT_APP(Updater::App);