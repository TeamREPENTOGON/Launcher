#include "shared/externals.h"
#include "launcher/app.h"
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
	if (!configurationOk) {
		Logger::Warn("Unable to load configuration file, starting wizard\n");
	}

	__installation = new Installation(&__nopLogGUI, &__configuration);
	auto [isaacPath, repentogonOk] = __installation->Initialize(sCLI->IsaacPath());

	bool wizardOk = false, wizardRan = false;
	bool wizardInstalledRepentogon = false;
	bool isIsaacValid = __installation->GetIsaacInstallation().IsValid();
	if (sCLI->ForceWizard() || !configurationOk || !isIsaacValid) {
		if (sCLI->ForceWizard()) {
			Logger::Info("Force starting wizard due to command-line\n");
		} else if (!isIsaacValid) {
			Logger::Info("Starting wizard as no valid Isaac installation has been found\n");
		}

		wizardOk = RunWizard(&wizardInstalledRepentogon);
		wizardRan = true;
	}

	if (!__installation->GetIsaacInstallation().IsValid()) {
		wxMessageBox("The launcher was not able to find any valid Isaac installation.\n"
			"Either check your Steam installation, or provide the path to the executable through the --isaac option.",
			"Fatal error", wxOK | wxICON_EXCLAMATION);
		Logger::Fatal("No valid Isaac installation found\n");
		return false;
	}

	RepentogonInstallerFrame* installerFrame = nullptr;
	MainFrame* mainWindow = CreateMainWindow();

	/* Install Repentogon in the following cases:
	 *	a) The user requested a forced update
	 *  b) The wizard did not run and the user requested automatic updates in the configuration,
	 * or there is no installation of Repentogon available
	 *  c) The wizard ran and exited without performing an installation of Repentogon
	 */
	bool forcedUpdate = sCLI->ForceRepentogonUpdate();
	bool wizardLess = !wizardRan && (!repentogonOk || __installation->GetLauncherConfiguration()->HasAutomaticUpdates());
	bool wizardFull = wizardRan && !wizardOk && !wizardInstalledRepentogon;

	if (forcedUpdate || wizardLess || wizardFull) {
		if (forcedUpdate) {
			Logger::Info("Forcibly updating Repentogon\n");
		} else if (wizardLess) {
			if (!repentogonOk) {
				Logger::Info("Installing Repentogon: wizard was skipped and Isaac installation "
					"contains no valid Repentogon installation\n");
			} else {
				Logger::Info("Updating Repentogon due to auto-updates\n");
			}
		} else {
			Logger::Info("Installing Repentogon: wizard was exited without installing Repentogon\n");
		}

		installerFrame = CreateRepentogonInstallerWindow(sCLI->ForceRepentogonUpdate());
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