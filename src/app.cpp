#include "chained_future/chained_future.h"

#include "shared/externals.h"
#include "launcher/app.h"
#include "launcher/cli.h"
#include "launcher/installation.h"
#include "launcher/launcher_self_update.h"
#include "launcher/windows/launcher.h"
#include "launcher/windows/setup_wizard.h"
#include "shared/github_executor.h"
#include "shared/logger.h"
#include "shared/loggable_gui.h"
#include "launcher/windows/repentogon_installer.h"
#include <filesystem>
#include <launcher/modupdater.h>

static LauncherConfiguration __configuration;
static Launcher::Installation* __installation;
static NopLogGUI __nopLogGUI;

bool Launcher::App::RunWizard(Launcher::LauncherMainWindow* mainWindow, bool* installedRepentogon) {
	/* Do not do the same in the main window : if we reach the main window,
	 * the wizard has run. This doesn't change even if the user chooses to
	 * rerun the wizard from the main window.
	 *
	 * This is the only place where we are 100% sure the wizard has not run
	 * yet.
	 */
	__configuration.SetRanWizard(false);
	LauncherWizard* wizard = new LauncherWizard(mainWindow, __installation, &__configuration);
	wizard->AddPages(false);
	bool wizardOk = wizard->Run();
	*installedRepentogon = wizard->WasRepentogonInstalled(false);
	wizard->Destroy();

	return wizardOk;
}

RepentogonInstallerFrame* Launcher::App::CreateRepentogonInstallerWindow(
	LauncherMainWindow* mainWindow, bool forceUpdate, bool allowUnstable) {
	RepentogonInstallerFrame* frame = new RepentogonInstallerFrame(mainWindow,
		false, __installation, forceUpdate, allowUnstable);
	frame->Initialize();
	return frame;
}

Launcher::LauncherMainWindow* Launcher::App::CreateMainWindow() {
	LauncherMainWindow* frame = new LauncherMainWindow(__installation, &__configuration);
	return frame;
}

void SetWorkingDirToExe() {//stupid shit to make menu shortcuts work
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
	SetCurrentDirectoryA(exeDir.string().c_str());
}

bool Launcher::App::OnInit() {
	SetWorkingDirToExe();
	Logger::Init("launcher.log", "w");
	Externals::Init();

	sGithubExecutor->Start();
	if (sCLI->Parse(argc, argv) > 0) {
		Logger::Error("Syntax error while parsing command line\n");
	}

	__installation = new Installation(&__nopLogGUI, &__configuration);
	_mainFrame = CreateMainWindow();
	_mainFrame->EnableInterface(false);
	
	wxIcon icon;
	icon.CopyFromBitmap(wxBitmap(wxICON(IDI_ICON1)));
	_mainFrame->SetIcon(icon);

	if (!sCLI->SkipSelfUpdate()) {
		HandleSelfUpdate(_mainFrame, false, false);
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

		MessageBoxA(_mainFrame->GetHWND(), stream.str().c_str(), "REPENTOGON Launcher", MB_ICONERROR);
	}
	bool configurationOk = false;

	if (configurationPathOk) {
		LauncherConfigurationLoad loadResult;
		configurationOk = __configuration.Load(&loadResult);

		if (!configurationOk) {
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
	}

	_mainFrame->Show(!__configuration.AutoLaunch());

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
	if (!sCLI->SkipWizard() && !__configuration.IsaacExecutablePathHasOverride()) {
		if (sCLI->ForceWizard() || !configurationOk || !isIsaacValid || isStandalone || !__configuration.RanWizard()) {
			if (sCLI->ForceWizard()) {
				Logger::Info("Force starting wizard due to command-line\n");
			} else if (!isIsaacValid) {
				Logger::Info("Starting wizard as no valid Isaac installation has been found\n");
			}

			wizardOk = RunWizard(_mainFrame, &wizardInstalledRepentogon);
			wizardRan = true;
		}
	}

	if (!__installation->GetIsaacInstallation().GetMainInstallation().IsValid()) {
		wxMessageBox("The launcher was not able to find any valid Isaac installation.\n"
			"Either check your Steam installation, or provide the path to the executable through the --isaac option.",
			"Fatal error", wxOK | wxICON_EXCLAMATION, _mainFrame);
		Logger::Fatal("No valid Isaac installation found\n");
		return false;
	}

	RepentogonInstallerFrame* installerFrame = nullptr;

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

		installerFrame = CreateRepentogonInstallerWindow(_mainFrame,
			sCLI->ForceRepentogonUpdate(), __configuration.UnstableUpdates());
		installerFrame->SetMainFrame(_mainFrame);
		installerFrame->InstallRepentogon();
		installerFrame->Show();
	} else {
		_mainFrame->Init();
	}
	wxInitAllImageHandlers(); //needed for stupid modman thumb shit (dunno if it belongs here, but it felt nice to shove it here)

	return true;
}

int Launcher::App::OnExit() {
	delete __installation;
	Externals::End();
	return 0;
}

wxIMPLEMENT_APP(Launcher::App);