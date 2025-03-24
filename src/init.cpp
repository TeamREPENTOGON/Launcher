#include "shared/externals.h"
#include "launcher/installation.h"
#include "launcher/windows/launcher.h"
#include "launcher/windows/self_updater.h"
#include "shared/logger.h"
#include "shared/loggable_gui.h"
#include "shared/github_executor.h"
#include "launcher/windows/setup_wizard.h"

static LauncherConfiguration* __configuration;
static Launcher::Installation* __installation;
static NopLogGUI __nopLogGUI;

bool Launcher::App::OnInit() {
	Logger::Init("launcher.log", "w");
	Externals::Init();
	sGithubExecutor->Start();

	SelfUpdaterWindow* updater = new SelfUpdaterWindow();
	const bool selfUpdatedStarted = updater->HandleSelfUpdate();
	updater->Hide();
	updater->Destroy();
	if (selfUpdatedStarted) {
		Logger::Info("Self-update initiated. Closing...\n");
		Exit();
		return true;
	}

	__configuration = new LauncherConfiguration();
	bool configurationOk = __configuration->Load(nullptr);
	__installation = new Installation(&__nopLogGUI, __configuration);

	__installation->Initialize();

	bool wizardOk = false;
	if (!configurationOk || !__installation->GetIsaacInstallation().IsValid()) {
		LauncherWizard* wizard = new LauncherWizard(__installation);
		wizard->AddPages();
		wizardOk = wizard->Run();
	}

	MainFrame* frame = new MainFrame(__installation);
	frame->PreInit();
	frame->Show();

	return true;
}

int Launcher::App::OnExit() {
	delete __installation;
	delete __configuration;
	Externals::End();
	return 0;
}

wxIMPLEMENT_APP(Launcher::App);