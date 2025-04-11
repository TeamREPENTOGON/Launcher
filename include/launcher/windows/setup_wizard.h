#pragma once

#include <atomic>
#include <thread>

#include "wx/checkbox.h"
#include "wx/gauge.h"
#include "wx/icon.h"
#include "wx/iconbndl.h"
#include "wx/richtext/richtextctrl.h"
#include "wx/stattext.h"
#include "wx/textctrl.h"
#include "wx/wizard.h"
#include "launcher/installation.h"

class LauncherWizard : public wxWizard {
public:
    LauncherWizard(Launcher::Installation* installation);

    void AddPages(bool skipIntroduction);
    bool Run();

    void OnPageChanged(wxWizardEvent& event);
    void BeforePageChanged(wxWizardEvent& event);
    void OnIsaacExecutableSelected(wxCommandEvent& event);
    void OnUnstableUpdatesCheckBoxClicked(wxCommandEvent& event);

    inline bool WasRepentogonInstalled() const {
        return _repentogonInstallationDone;
    }

private:
    void AddIntroductionPage();
    void AddIsaacSetupPage();
    void AddRepentogonSetupPage();
    void AddRepentogonInstallationPage();
    void AddCompletionPage();

    /* Configures the widgets on the Repentogon setup page based on the
     * the current Isaac installation and available launcher configuration.
     * This function refreshes the values of widgets on the fly.
     * 
     * All widgets need to have been created and initialized.
     */
    void ConfigureRepentogonSetupPage();

    void UpdateIsaacSetupNextButton();
    void UpdateIsaacPath(std::string const& path);
    void UpdateIsaacExecutableInfo();
    void UpdateRepentogonInstallationNavigationButtons();
    void SetupCompatibilityWithRepentogonText();

    /* Called when the user changes from the Isaac setup page to the Repentogon
     * setup page.
     * 
     * If the Isaac installation is not compatible with Repentogon, immediately
     * prompt the user on whether they want to go through with the installation
     * and/or configuration of Repentogon.
     * 
     * Return true if the user wants to proceed, false otherwise.
     */
    bool CheckRepentogonCompatibilityOnPageChange();

    void PromptIsaacExecutable();

    void OnIsaacExecutableSelected(std::string const& path);

    void StartRepentogonInstallation();
    void RepentogonInstallationThread();

    wxWizardPageSimple* _introductionPage = nullptr;
    wxWizardPageSimple* _isaacSetupPage = nullptr;
    wxWizardPageSimple* _repentogonSetupPage = nullptr;
    wxWizardPageSimple* _repentogonInstallationPage = nullptr;
    wxWizardPageSimple* _completionPage = nullptr;

    Launcher::Installation* _installation = nullptr;

    struct {
        wxStaticText* _topText = nullptr;
        wxTextCtrl* _selectedExecutableControl = nullptr;
        wxButton* _selectButton = nullptr;
        wxStaticText* _executableVersionBaseText = nullptr;
        wxStaticText* _executableVersionValueText = nullptr;
        wxStaticText* _compatibleWithRepentogonText = nullptr;
        wxStaticText* _compatibleWithRepentogonValueText = nullptr;
    } _isaacSetup;

    struct {
        wxStaticText* _topText = nullptr;
        wxStaticText* _installationState = nullptr;
        wxCheckBox* _unstableUpdates = nullptr;
        wxCheckBox* _autoUpdates = nullptr;
        wxStaticText* _updateWarning = nullptr;
    } _repentogonSetup;

    struct {
        wxTextCtrl* _logText = nullptr;
        wxGauge* _gauge = nullptr;
    } _repentogonInstallation;

    bool _isaacFound = false;
    bool _compatibleWithRepentogon = false;
    bool _repentogonInstallationDone = false;
    bool _dirtyUnstableUpdates = false;
    wxIcon _questionMark;
    wxBitmap _questionMarkBitmap;

    std::thread _repentogonInstallerThread;

    wxDECLARE_EVENT_TABLE();
};