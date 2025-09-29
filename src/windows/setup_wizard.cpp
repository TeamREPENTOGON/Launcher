#include "launcher/cli.h"
#include "launcher/cancel.h"
#include "launcher/windows/launcher.h"
#include "launcher/log_helpers.h"
#include "launcher/repentogon_installer.h"
#include "launcher/windows/setup_wizard.h"

#include "shared/logger.h"

#include "wx/button.h"
#include "wx/filedlg.h"
#include "wx/msgdlg.h"
#include "wx/sizer.h"
#include "wx/statbmp.h"
#include "wx/statbox.h"
#include "wx/stattext.h"
#include "wx/textctrl.h"
#include "wx/tooltip.h"

enum {
    LAUNCHER_WIZARD_CONTROL_WIZARD,
    LAUNCHER_WIZARD_CONTROL_ISAAC_SETUP_BUTTON,
    LAUNCHER_WIZARD_CONTROL_UNSTABLE_UPDATES_CHECKBOX,
    LAUNCHER_WIZARD_CONTROL_AUTOMATIC_UPDATES_CHECKBOX
};

wxBEGIN_EVENT_TABLE(LauncherWizard, wxWizard)
EVT_WIZARD_PAGE_CHANGED(LAUNCHER_WIZARD_CONTROL_WIZARD, LauncherWizard::OnPageChanged)
EVT_BUTTON(LAUNCHER_WIZARD_CONTROL_ISAAC_SETUP_BUTTON, LauncherWizard::OnIsaacExecutableSelected)
EVT_WIZARD_BEFORE_PAGE_CHANGED(LAUNCHER_WIZARD_CONTROL_WIZARD, LauncherWizard::BeforePageChanged)
EVT_CHECKBOX(LAUNCHER_WIZARD_CONTROL_UNSTABLE_UPDATES_CHECKBOX, LauncherWizard::OnUnstableUpdatesCheckBoxClicked)
EVT_CHECKBOX(LAUNCHER_WIZARD_CONTROL_AUTOMATIC_UPDATES_CHECKBOX, LauncherWizard::OnAutomaticUpdatesCheckBoxClicked)
EVT_WIZARD_CANCEL(LAUNCHER_WIZARD_CONTROL_WIZARD, LauncherWizard::OnCancel)
wxEND_EVENT_TABLE()

LauncherWizard::LauncherWizard(Launcher::LauncherMainWindow* mainWindow,
    Launcher::Installation* installation,
    LauncherConfiguration* configuration) :
    _installation(installation), _configuration(configuration),
    _questionMark(L"wxICON_QUESTION", wxBITMAP_TYPE_ICO_RESOURCE),
    wxWizard(mainWindow, LAUNCHER_WIZARD_CONTROL_WIZARD, "REPENTOGON Launcher Setup") {
    _questionMarkBitmap.CopyFromIcon(_questionMark);
    wxBitmap::Rescale(_questionMarkBitmap, wxSize(16, 16));
}

bool LauncherWizard::Run() {
    return RunWizard(_introductionPage ? _introductionPage : _isaacSetupPage);
}

void LauncherWizard::AddPages(bool skipIntroduction) {
    SetPageSize(wxSize(640, 480));

    if (!skipIntroduction) {
        AddIntroductionPage();
    }

    AddIsaacSetupPage();
    AddRepentogonSetupPage();
    AddRepentogonInstallationPage();
    AddCompletionPage();

    /* Chain the pages even if it allows the user to skip some parts. The
     * chaining must be in place for wxWidgets to properly display the
     * "Next" button instead of "Finish". The tricky pages manage that by
     * themselves.
     */
    if (!skipIntroduction) {
        _introductionPage->Chain(_isaacSetupPage);
    }
    _isaacSetupPage->Chain(_repentogonSetupPage)
        .Chain(_repentogonInstallationPage)
        .Chain(_completionPage);

    if (!skipIntroduction) {
        GetPageAreaSizer()->Add(_introductionPage);
    } else {
        GetPageAreaSizer()->Add(_isaacSetupPage);
    }

    // GetPageAreaSizer()->Fit(this);
}

void LauncherWizard::AddIntroductionPage() {
    wxWizardPageSimple* page = new wxWizardPageSimple(this);
    wxStaticText* text = new wxStaticText(page, wxID_ANY, "Welcome to the REPENTOGON Launcher setup."
        " This wizard will guide through the initial installation and configuration of REPENTOGON.",
        wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    wxSizerFlags flags = wxSizerFlags().Expand();
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, flags);
    page->SetSizerAndFit(sizer);
    //page->SetSizer(sizer);
    _introductionPage = page;
}

void LauncherWizard::AddIsaacSetupPage() {
    wxWizardPageSimple* page = new wxWizardPageSimple(this);
    wxSizerFlags flags = wxSizerFlags().Expand();

    std::optional<std::string> path = std::nullopt;
    InstallationData const& isaacInstallation = _installation->GetIsaacInstallation()
        .GetMainInstallation();
    if (isaacInstallation.IsValid()) {
        path = isaacInstallation.GetExePath();
    }

    /* Always offer the possibility to change the location. However, the user
     * won't be allowed to continue if the location is invalid.
     */
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* topText = new wxStaticText(page, wxID_ANY, "");
    topText->SetLabel("Please select the Binding of Isaac Repentance+ executable. Press Next once it is done.");
    sizer->Add(topText, flags);

    wxBoxSizer* selectionSizer = new wxBoxSizer(wxHORIZONTAL);
    wxTextCtrl* selectedExecutableControl = new wxTextCtrl(page, wxID_ANY);
    if (path) {
        selectedExecutableControl->SetValue(*path);
    } else {
        selectedExecutableControl->SetDefaultStyle(wxTextAttr(*wxRED));
        selectedExecutableControl->SetValue("No file provided");
    }

    wxButton* button = new wxButton(page, LAUNCHER_WIZARD_CONTROL_ISAAC_SETUP_BUTTON, "Select executable...");

    wxSizerFlags selectionFlags = wxSizerFlags().Expand();
    selectionSizer->Add(selectedExecutableControl, selectionFlags.Proportion(1));
    selectionSizer->Add(button);

    sizer->Add(selectionSizer, flags);

    wxStaticBox* infoBox = new wxStaticBox(page, wxID_ANY, "Executable information");
    wxSizer* infoSizer = new wxStaticBoxSizer(infoBox, wxVERTICAL);

    wxSizerFlags textBorder = wxSizerFlags().Border(wxLEFT, 10);

    wxStaticText* executableVersionText = new wxStaticText(infoBox, wxID_ANY, "Version: ");
    wxStaticText* executableVersionValueText = new wxStaticText(infoBox, wxID_ANY, "");
    const char* version = isaacInstallation.GetVersion();
    if (version) {
        executableVersionValueText->SetLabel(version);
    } else {
        executableVersionValueText->SetLabel("unknown");
    }

    wxStaticText* compatibilityWithRepentogon = new wxStaticText(infoBox, wxID_ANY, "Compatible with Repentogon: ");
    wxStaticText* compatibleWithRepentogon = new wxStaticText(infoBox, wxID_ANY, "");

    _isaacSetup._compatibleWithRepentogonValueText = compatibleWithRepentogon;
    SetupCompatibilityWithRepentogonText();

    wxSizer* compatibilitySizer = new wxBoxSizer(wxHORIZONTAL);
    compatibilitySizer->Add(compatibilityWithRepentogon, textBorder);
    compatibilitySizer->Add(compatibleWithRepentogon);

    wxSizer* versionSizer = new wxBoxSizer(wxHORIZONTAL);
    versionSizer->Add(executableVersionText, textBorder);
    versionSizer->Add(executableVersionValueText);

    infoSizer->Add(versionSizer, flags);
    infoSizer->Add(compatibilitySizer, flags);
    // infoBox->SetSizerAndFit(infoSizer);
    sizer->Add(infoSizer, flags);
    page->SetSizerAndFit(sizer);
    //page->SetSizer(sizer);

    _isaacSetup._topText = topText;
    _isaacSetup._selectButton = button;
    _isaacSetup._selectedExecutableControl = selectedExecutableControl;
    _isaacSetup._executableVersionBaseText = executableVersionText;
    _isaacSetup._executableVersionValueText = executableVersionValueText;
    _isaacSetup._compatibleWithRepentogonText = compatibilityWithRepentogon;
    _isaacSetup._compatibleWithRepentogonValueText = compatibleWithRepentogon;

    _isaacSetupPage = page;

    _isaacFound = (bool)path;
}

void LauncherWizard::AddRepentogonSetupPage() {
    wxWizardPageSimple* page = new wxWizardPageSimple(this);
    wxSizerFlags flags = wxSizerFlags().Expand();
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* topText = new wxStaticText(page, wxID_ANY, "Select the options to configure how the launcher will manage Repentogon. "
        "Press Next once you are done.");

    wxStaticText* installationText = new wxStaticText(page, wxID_ANY, wxEmptyString);
    wxCheckBox* automaticUpdates = new wxCheckBox(page,
        LAUNCHER_WIZARD_CONTROL_AUTOMATIC_UPDATES_CHECKBOX,
        "Automatic updates (recommended)");
    automaticUpdates->SetValue(true);
    wxCheckBox* unstableUpdates = new wxCheckBox(page,
        LAUNCHER_WIZARD_CONTROL_UNSTABLE_UPDATES_CHECKBOX,
        "Unstable releases (not recommended)");

    wxSizer* automaticUpdatesSizer = new wxBoxSizer(wxHORIZONTAL);
    automaticUpdatesSizer->Add(automaticUpdates);
    wxStaticBitmap* automaticUpdatesBitmap = new wxStaticBitmap(page, wxID_ANY, _questionMarkBitmap);
    wxToolTip* automaticUpdatesTooltip = new wxToolTip("Request the launcher to check, on startup, if a Repentogon update "
        "is available and prompt your for update.\n\n"
        "If not selected, you can manage Repentogon by yourself or manually request an update.");
    automaticUpdatesBitmap->SetToolTip(automaticUpdatesTooltip);
    automaticUpdatesSizer->Add(automaticUpdatesBitmap);

    wxSizer* unstableUpdatesSizer = new wxBoxSizer(wxHORIZONTAL);
    unstableUpdatesSizer->Add(unstableUpdates);
    wxStaticBitmap* unstableUpdatesBitmap = new wxStaticBitmap(page, wxID_ANY, _questionMarkBitmap);
    wxToolTip* unstableUpdatesTooltip = new wxToolTip("If selected, the launcher will include unstable (pre-releases) "
        "Repentogon updates in its updates search.\n\n"
        "Unless you are a mod developer and rely on experimental features, you probably do not want this.");
    unstableUpdatesBitmap->SetToolTip(unstableUpdatesTooltip);
    unstableUpdatesSizer->Add(unstableUpdatesBitmap);

    wxStaticText* warningText = new wxStaticText(page, wxID_ANY, "");
    warningText->SetForegroundColour(*wxRED);

    sizer->Add(topText, flags);
    sizer->Add(installationText, flags);
    sizer->Add(automaticUpdatesSizer, flags);
    sizer->Add(unstableUpdatesSizer, flags);
    sizer->Add(warningText, flags);

    _repentogonSetup._installationState = installationText;
    _repentogonSetup._autoUpdates = automaticUpdates;
    _repentogonSetup._unstableUpdates = unstableUpdates;
    _repentogonSetup._topText = topText;
    _repentogonSetup._updateWarning = warningText;

    ConfigureRepentogonSetupPage();

    page->SetSizerAndFit(sizer);
    //page->SetSizer(sizer);
    _repentogonSetupPage = page;
}

void LauncherWizard::ConfigureRepentogonSetupPage() {
    wxStaticText* installationText = _repentogonSetup._installationState;
    RepentogonInstallation const& repentogon = _installation->GetRepentogonInstallation();
    if (repentogon.IsValid()) {
        std::ostringstream stream;
        stream << "Found a valid installation of Repentogon (version " << repentogon.GetRepentogonVersion() << ")";
        installationText->SetLabel(stream.str());
    } else {
        installationText->SetLabel("No valid installation of Repentogon found. An installation will be performed.");
    }

    _repentogonSetup._autoUpdates->SetValue(_configuration->AutomaticUpdates());
    _repentogonSetup._autoUpdates->Enable(!_configuration->AutomaticUpdatesHasOverride());

    _repentogonSetup._unstableUpdates->SetValue(_configuration->UnstableUpdates());
    _repentogonSetup._unstableUpdates->Enable(!_configuration->UnstableUpdatesHasOverride());
}

void LauncherWizard::AddRepentogonInstallationPage() {
    wxWizardPageSimple* page = new wxWizardPageSimple(this);
    wxSizerFlags flags = wxSizerFlags().Expand();
    wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    wxTextCtrl* log = new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH);
    log->SetSizeHints(wxSize(0, 400));
    wxGauge* gauge = new wxGauge(page, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL | wxGA_PROGRESS);
    sizer->Add(log, flags);
    sizer->Add(gauge, flags);
    page->SetSizerAndFit(sizer);
    // page->SetSizer(sizer);

    _repentogonInstallation._logText = log;
    _repentogonInstallation._gauge = gauge;

    _repentogonInstallationPage = page;
}

void LauncherWizard::AddCompletionPage() {
    wxWizardPageSimple* page = new wxWizardPageSimple(this);
    wxSizerFlags flags = wxSizerFlags().Expand();
    _finalPage._text = new wxStaticText(page, wxID_ANY, "");
    _completionPage = page;
}

void LauncherWizard::OnPageChanged(wxWizardEvent& event) {
    wxWizardPage* source = event.GetPage();
    source->Layout();
    if (source == _isaacSetupPage) {
        UpdateIsaacSetupNextButton();
        _isaacSetup._selectButton->Enable(!_configuration->IsaacExecutablePathHasOverride());
    } else if (source == _repentogonSetupPage) {
        ConfigureRepentogonSetupPage();
    } else if (source == _repentogonInstallationPage) {
        UpdateRepentogonInstallationNavigationButtons();
        std::unique_lock<std::recursive_mutex> lck(_installerMutex);
        if (!_installer) {
            StartRepentogonInstallation();
        }
    } else if (source == _introductionPage) {
        if (wxWindow* button = FindWindowById(wxID_FORWARD, this)) {
            button->Enable(true);
        }
    }
}

void LauncherWizard::UpdateIsaacSetupNextButton() {
    wxWindow* button = FindWindowById(wxID_FORWARD, this);
    if (button) {
        button->Enable(_isaacFound);
    }
}

void LauncherWizard::UpdateRepentogonInstallationNavigationButtons() {
    wxWindow* prev = FindWindowById(wxID_BACKWARD, this);
    wxWindow* next = FindWindowById(wxID_FORWARD, this);

    std::unique_lock<std::recursive_mutex> lck(_installerMutex);
    bool hasCompleted = _installer && _installer->HasCompleted(true);
    if (prev) {
        prev->Enable(hasCompleted);
    }

    if (next) {
        next->Enable(hasCompleted);
    }
}

void LauncherWizard::OnRepentogonInstallationCompleted(bool finished,
    Launcher::RepentogonInstaller::DownloadInstallRepentogonResult result) {
    _installerFinished = finished;
    _downloadInstallResult = result;

    UpdateFinalPage(finished, result);
    UpdateRepentogonInstallationNavigationButtons();
}

void LauncherWizard::UpdateFinalPage(bool finished,
    Launcher::RepentogonInstaller::DownloadInstallRepentogonResult result) {
    if (!finished) {
        _finalPage._text->SetLabel("The installation of Repentogon was cancelled.\n");
    } else {
        using r = Launcher::RepentogonInstaller;
        switch (result) {
            case r::DOWNLOAD_INSTALL_REPENTOGON_ERR:
            case r::DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES:
                _finalPage._text->SetLabel("An error occured during the installation of Repentogon.\n"
                    "Please check the log file for more information.");
                break;

            case r::DOWNLOAD_INSTALL_REPENTOGON_OK:
                _finalPage._text->SetLabel("Repentogon has been successfully installed.");
                break;

            case r::DOWNLOAD_INSTALL_REPENTOGON_UTD:
                _finalPage._text->SetLabel("The provided installation of Isaac already contained an up-to-date\n"
                    "installation of Repentogon, no operation has been performed.");
                break;

            case r::DOWNLOAD_INSTALL_REPENTOGON_NONE:
                wxMessageBox("There appears to be a bug somewhere, the Repentogon installer seemingly did not run.\n"
                    "Please open a ticket on Github and provide the launcher.log file.",
                    "Internal error", wxOK, this);
                break;

            default:
                Logger::Error("LauncherWizard::UpdateFinalPage: Unexpected error "
                    "code from the Repentogon installer: %d\n", result);
                wxMessageBox("There appears to be a bug somewhere, the Repentogon installer exited with an unexpected\n"
                    "error code. Please open a ticket on Github and provide the launcer.log file.",
                    "Internal error", wxOK, this);
                break;
        }
    }
}

void LauncherWizard::StartRepentogonInstallation() {
    _configuration->SetRanWizard(true);
    std::unique_lock<std::recursive_mutex> lck(_installerMutex);
    // Force a full reinstall if the current installation is broken, even if rgon itself is up to date.
    const bool force = !_installation->GetRepentogonInstallation().IsValid()
        || !_installation->GetIsaacInstallation().GetRepentogonInstallation().IsValid()
        || !_installation->GetIsaacInstallation().GetRepentogonInstallation().IsCompatibleWithRepentogon();
    _installer = std::make_unique<RepentogonInstallerHelper>(this, _installation,
        _repentogonSetup._unstableUpdates->GetValue(), force, _repentogonInstallation._logText);
    _installer->Install(std::bind_front(&LauncherWizard::OnRepentogonInstallationCompleted, this));
}

void LauncherWizard::OnIsaacExecutableSelected(wxCommandEvent&) {
    PromptIsaacExecutable();
}

void LauncherWizard::OnIsaacExecutableSelected(std::string const& path) {
    if (path.empty()) {
        return;
    }

    bool standalone = false;
    _isaacFound = _installation->SetIsaacExecutable(path, &standalone);
    if (!_isaacFound) {
        wxString message = "The executable you selected (" + path + ") is not valid";
        wxMessageDialog dialog(this, message, "Invalid executable", wxOK | wxICON_ERROR);
        dialog.ShowModal();
        return;
    }

    if (standalone) {
        if (wxMessageBox("The executable you selected is a Repentogon-patched specific executable.\n "
            "Proceeding with this executable is not advised. Continue with it ?",
            "Warning", wxYES_NO | wxICON_WARNING, this) == wxNO) {
            return;
        }
    }

    UpdateIsaacPath(path);
    UpdateIsaacSetupNextButton();
    UpdateIsaacExecutableInfo();
}

void LauncherWizard::UpdateIsaacPath(std::string const& path) {
    wxASSERT(_installation->GetIsaacInstallation().GetMainInstallation().IsValid());
    _isaacSetup._selectedExecutableControl->SetValue(path);
}

void LauncherWizard::UpdateIsaacExecutableInfo() {
    InstallationData const data = _installation->GetIsaacInstallation().GetMainInstallation();
    wxASSERT(data.IsValid());
    const char* version = data.GetVersion();
    SetupCompatibilityWithRepentogonText();
    _isaacSetup._executableVersionValueText->SetLabel(version);
}

void LauncherWizard::SetupCompatibilityWithRepentogonText() {
    IsaacInstallation const& isaac = _installation->GetIsaacInstallation();
    InstallationData const& isaacData = isaac.GetMainInstallation();
    wxStaticText* compatibleWithRepentogon = _isaacSetup._compatibleWithRepentogonValueText;

    if (isaacData.IsValid()) {
        if (isaacData.IsCompatibleWithRepentogon()) {
            compatibleWithRepentogon->SetForegroundColour(wxColour(23, 122, 23));
            compatibleWithRepentogon->SetLabel("yes");
            _compatibleWithRepentogon = true;
        } else {
            compatibleWithRepentogon->SetForegroundColour(*wxRED);
            compatibleWithRepentogon->SetLabel("no");
            _compatibleWithRepentogon = false;
        }
    } else {
        compatibleWithRepentogon->SetForegroundColour(*wxBLACK);
        compatibleWithRepentogon->SetLabel("unknown");
        _compatibleWithRepentogon = false;
    }
}

void LauncherWizard::PromptIsaacExecutable() {
    wxFileDialog dialog(this, "Select the Binding of Isaac Repentance+ executable", wxEmptyString,
        wxEmptyString, wxEmptyString, wxFD_FILE_MUST_EXIST, wxDefaultPosition,
        wxDefaultSize, "Select Isaac executable");
    dialog.ShowModal();
    std::string path = dialog.GetPath().ToStdString();
    OnIsaacExecutableSelected(path);
}

void LauncherWizard::BeforePageChanged(wxWizardEvent& event) {
    if (event.GetPage() == _isaacSetupPage && event.GetDirection()) {
        bool shouldContinue = CheckRepentogonCompatibilityOnPageChange();
        if (!shouldContinue) {
            event.Veto();
            Close();
        }
    }
}

bool LauncherWizard::CheckRepentogonCompatibilityOnPageChange() {
    if (_compatibleWithRepentogon) {
        return true;
    }

    wxMessageDialog dialog(this, "This version of Isaac is not compatible with Repentogon.\n"
        "Do you still want to proceed with the configuration and installation of Repentogon ?",
        "Incompatibility with Repentogon",
        wxYES_NO);
    int result = dialog.ShowModal();
    return result == wxID_YES;
}

void LauncherWizard::OnUnstableUpdatesCheckBoxClicked(wxCommandEvent& event) {
    RepentogonInstallation const& repentogon = _installation->GetRepentogonInstallation();
    if (repentogon.IsValid()) {
        _dirtyUnstableUpdates = !_dirtyUnstableUpdates;
        if (_dirtyUnstableUpdates) {
            _repentogonSetup._updateWarning->SetLabel("This change in options "
                "may require an update to your existing Repentogon installation");
        } else {
            _repentogonSetup._updateWarning->SetLabel("");
        }
    }

    _configuration->SetUnstableUpdates(((wxCheckBox*)event.GetEventObject())->GetValue());
}

void LauncherWizard::OnAutomaticUpdatesCheckBoxClicked(wxCommandEvent& event) {
    _configuration->SetAutomaticUpdates(((wxCheckBox*)event.GetEventObject())->GetValue());
}

void LauncherWizard::OnCancel(wxWizardEvent& event) {
    std::unique_lock<std::recursive_mutex> lck(_installerMutex);
    if (_installer) {
        switch (_installer->Terminate()) {
        case RepentogonInstallerHelper::TERMINATE_CANCEL_REJECTED:
            event.Veto();
            break;

        default:
            break;
        }
    }
}