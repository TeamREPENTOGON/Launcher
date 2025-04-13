#include "shared/externals.h"
#include "launcher/cli.h"
#include "launcher/installation.h"
#include "launcher/launcher_self_update.h"
#include "launcher/windows/launcher.h"
#include "launcher/windows/setup_wizard.h"
#include "shared/externals.h"
#include "shared/github_executor.h"
#include "shared/logger.h"
#include "shared/loggable_gui.h"
#include "shared/github_executor.h"
#include "launcher/windows/setup_wizard.h"
#include "launcher/windows/repentogon_installer.h"

static LauncherConfiguration __configuration;
static Launcher::Installation* __installation;
static NopLogGUI __nopLogGUI;

bool Launcher::App::RunWizard(bool* installedRepentogon) {
	LauncherWizard* wizard = new LauncherWizard(__installation);
	wizard->AddPages(false);
	bool wizardOk = wizard->Run();
	*installedRepentogon = wizard->WasRepentogonInstalled();
	wizard->Destroy();

	return wizardOk;
}

RepentogonInstallerFrame* Launcher::App::CreateRepentogonInstallerWindow(bool forceUpdate) {
	RepentogonInstallerFrame* frame = new RepentogonInstallerFrame(__installation, forceUpdate);
	frame->Initialize();
	return frame;
}

Launcher::MainFrame* Launcher::App::CreateMainWindow() {
	MainFrame* frame = new MainFrame(__installation);
	return frame;
}

bool Launcher::App::OnInit() {
	Logger::Init("launcher.log", "w");
	Externals::Init();

	sGithubExecutor->Start();
	if (sCLI->Parse(argc, argv) > 0) {
		Logger::Error("Syntax error while parsing command line\n");
	}

	if (!sCLI->SkipSelfUpdate()) {
		// Check for an available self-update.
		// If one is initiated, the launcher should get terminated by the updater.
		if (!Launcher::CheckForSelfUpdate(false)) {
			MessageBoxA(NULL, "Failed check for self-update. Check the log file for more details.\n", "REPENTOGON Launcher", MB_ICONERROR);
		}
	}

	bool configurationOk = __configuration.Load(nullptr, sCLI->ConfigurationPath());

	__installation = new Installation(&__nopLogGUI, &__configuration);
	__installation->Initialize();

	bool wizardOk = false, wizardRan = false;
	bool wizardInstalledRepentogon = false;
	if (sCLI->ForceWizard() || !configurationOk || !__installation->GetIsaacInstallation().IsValid()) {
		wizardOk = RunWizard(&wizardInstalledRepentogon);
		wizardRan = true;
	}

	if (!__installation->GetIsaacInstallation().IsValid()) {
		Logger::Fatal("No valid Isaac installation found\n");
		return false;
	}

	RepentogonInstallerFrame* installerFrame = nullptr;
	/* Install Repentogon in the following cases:
	 *	a) The user requested a forced update
	 *  b) The wizard did not run and the user requested automatic updates in the configuration
	 *  c) The wizard ran and exited without performing an installation of Repentogon
	 */
	if (sCLI->ForceRepentogonUpdate() ||
		(!wizardRan && __installation->GetLauncherConfiguration()->HasAutomaticUpdates()) ||
		(wizardRan && !wizardOk && !wizardInstalledRepentogon)) {
		installerFrame = CreateRepentogonInstallerWindow(sCLI->ForceRepentogonUpdate());
	}

	MainFrame* mainWindow = CreateMainWindow();
	if (installerFrame) {
		installerFrame->SetMainFrame(mainWindow);
		installerFrame->InstallRepentogon();
		installerFrame->Show();
	} else {
		mainWindow->PreInit();
		mainWindow->Show();
	}

	return true;
}

int Launcher::App::OnExit() {
	delete __installation;
	Externals::End();
	return 0;
}

wxIMPLEMENT_APP(Launcher::App);