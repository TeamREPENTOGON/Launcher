#include "shared/externals.h"
#include "launcher/windows/launcher.h"
#include "launcher/windows/self_updater.h"
#include "shared/logger.h"
#include"shared/loggable_gui.h"
#include "launcher/self_updater/launcher_update_manager.h"


bool Launcher::App::OnInit() {
	Logger::Init("launcher.log", "w");
	Externals::Init();

	SelfUpdaterWindow* updater = new SelfUpdaterWindow();
	// updater->Show();
	updater->HandleSelfUpdate();
	updater->Destroy();

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