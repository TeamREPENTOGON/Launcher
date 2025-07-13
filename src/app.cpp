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
	LauncherWizard* wizard = new LauncherWizard(__installation, &__configuration);
	wizard->AddPages(false);
	bool wizardOk = wizard->Run();
	*installedRepentogon = wizard->WasRepentogonInstalled(false);
	wizard->Destroy();

	return wizardOk;
}

RepentogonInstallerFrame* Launcher::App::CreateRepentogonInstallerWindow(bool forceUpdate,
	bool allowUnstable) {
	RepentogonInstallerFrame* frame = new RepentogonInstallerFrame(nullptr,
		false, __installation, forceUpdate, allowUnstable);
	frame->Initialize();
	return frame;
}

Launcher::MainFrame* Launcher::App::CreateMainWindow() {
	MainFrame* frame = new MainFrame(__installation, &__configuration);
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
		/* Check for an available self-update.
		 * If one is initiated, the launcher should get terminated by the updater.
		 */
		if (!Launcher::CheckForSelfUpdate(false)) {
			MessageBoxA(NULL, "Failed check for self-update. Check the log file for more details.\n", "REPENTOGON Launcher", MB_ICONERROR);
		}
	}

	LauncherConfigurationInitialize initializationResult;
	std::optional<std::string> const& configurationHint = sCLI->ConfigurationPath();
	bool configurationPathOk = LauncherConfiguration::InitializeConfigurationPath(
		&initializationResult, configurationHint);

	if (!configurationPathOk) {
		std::ostringstream stream;
		if (configurationHint) {
			stream << "The specified configuration file " << *configurationHint <<
				" does not exist / cannot be created";
		} else {
			stream << "The launcher was unable to locate and/or create the default "
				"configuration file. Make sure you have the right to create files in "
				"\"%USERPROFILE%/Documents/My Games\".";
		}

		stream << std::endl << std::endl <<
			"The launcher will be unable to save its configuration and will prompt "
			"you again the next time.";

		MessageBoxA(NULL, stream.str().c_str(), "REPENTOGON Launcher", MB_ICONERROR);
	}
	LauncherConfigurationLoad loadResult;
	bool configurationOk = false;

	if (configurationPathOk) {
		configurationOk = __configuration.Load(&loadResult);
	}

	if (configurationPathOk && !configurationOk) {
		if (loadResult != LAUNCHER_CONFIGURATION_LOAD_NO_ISAAC) {
			std::ostringstream stream;
			stream << "The launcher was unable to process the configuration file " <<
				LauncherConfiguration::GetConfigurationPath() << ". It will run "
				"the setup wizard again." << std::endl;
			MessageBoxA(NULL, stream.str().c_str(), "REPENTOGON Launcher", MB_ICONERROR);
			Logger::Warn("Unable to load configuration file, starting wizard\n");
		} else {
			Logger::Info("No Isaac path specified in the configuration file, "
				"assuming first time installation / clear configuration file.\n");
		}
	}

	__installation = new Installation(&__nopLogGUI, &__configuration);
	std::optional<std::string> providedPath;
	std::string cliIsaacPath = sCLI->IsaacPath();
	if (!cliIsaacPath.empty()) {
		providedPath = std::move(cliIsaacPath);
	}

	if (!providedPath && configurationOk) {
		providedPath = __configuration.IsaacExecutablePath();
	}

	bool isStandalone = false;
	auto [isaacPath, repentogonOk] = __installation->Initialize(providedPath, &isStandalone);

	bool wizardOk = false, wizardRan = false;
	bool wizardInstalledRepentogon = false;
	bool isIsaacValid = __installation->GetIsaacInstallation().GetMainInstallation().IsValid();
	if (!sCLI->SkipWizard()) {
		if (sCLI->ForceWizard() || !configurationOk || !isIsaacValid || isStandalone) {
			if (sCLI->ForceWizard()) {
				Logger::Info("Force starting wizard due to command-line\n");
			} else if (!isIsaacValid) {
				Logger::Info("Starting wizard as no valid Isaac installation has been found\n");
			}

			wizardOk = RunWizard(&wizardInstalledRepentogon);
			wizardRan = true;
		}
	}

	if (!__installation->GetIsaacInstallation().GetMainInstallation().IsValid()) {
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
	 */
	bool forcedUpdate = sCLI->ForceRepentogonUpdate();
	bool wizardLess = !wizardRan && (!repentogonOk || __installation->GetLauncherConfiguration()->AutomaticUpdates());

	if (!sCLI->SkipRepentogonUpdate() && (forcedUpdate || wizardLess)) {
		if (forcedUpdate) {
			Logger::Info("Forcibly updating Repentogon\n");
		} else {
			if (!repentogonOk) {
				Logger::Info("Installing Repentogon: wizard was skipped and Isaac installation "
					"contains no valid Repentogon installation\n");
			} else {
				Logger::Info("Updating Repentogon due to auto-updates\n");
			}
		}

		installerFrame = CreateRepentogonInstallerWindow(sCLI->ForceRepentogonUpdate(),
			__configuration.UnstableUpdates());
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