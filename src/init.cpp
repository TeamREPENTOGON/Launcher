#include "shared/externals.h"
#include "launcher/launcher.h"
#include "shared/logger.h"

bool Launcher::App::OnInit() {
	Logger::Init("launcher.log", "w");
	Externals::Init();

	MainFrame* frame = new MainFrame();
	frame->Show();
	frame->PostInit();

	return true;
}

int Launcher::App::OnExit() {
	Externals::End();
	return 0;
}

wxIMPLEMENT_APP(Launcher::App);