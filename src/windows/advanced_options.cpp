#include <WinSock2.h>

#include "launcher/windows/advanced_options.h"
#include "launcher/windows/launcher.h"

wxBEGIN_EVENT_TABLE(Launcher::AdvancedOptionsWindow, wxDialog)
EVT_BUTTON(Launcher::AdvancedOptionsWindow::Controls::ADVANCED_CONTROLS_BUTTON_FORCE_UNSTABLE_UPDATE,
	Launcher::AdvancedOptionsWindow::OnButtonSelect)
EVT_BUTTON(Launcher::AdvancedOptionsWindow::Controls::ADVANCED_CONTROLS_BUTTON_FORCE_UPDATE,
	Launcher::AdvancedOptionsWindow::OnButtonSelect)
EVT_BUTTON(Launcher::AdvancedOptionsWindow::Controls::ADVANCED_CONTROLS_BUTTON_STABLE_SELF_UPDATE,
	Launcher::AdvancedOptionsWindow::OnButtonSelect)
EVT_BUTTON(Launcher::AdvancedOptionsWindow::Controls::ADVANCED_CONTROLS_BUTTON_UNSTABLE_SELF_UPDATE,
	Launcher::AdvancedOptionsWindow::OnButtonSelect)
wxEND_EVENT_TABLE()

namespace Launcher {
	AdvancedOptionsWindow::AdvancedOptionsWindow(LauncherMainWindow* mainFrame) : _mainFrame(mainFrame), wxDialog(mainFrame, -1, "Advanced options") {
		Build();
	}

	AdvancedOptionsWindow::~AdvancedOptionsWindow() {

	}

	void AdvancedOptionsWindow::OnButtonSelect(wxCommandEvent& event) {
		int id = ((wxButton*)event.GetEventObject())->GetId();
		int result = LauncherMainWindow::ADVANCED_EVENT_NONE;

		switch (id) {
		case Controls::ADVANCED_CONTROLS_BUTTON_FORCE_UPDATE:
			result = LauncherMainWindow::ADVANCED_EVENT_FORCE_REPENTOGON_UPDATE;
			break;

		case Controls::ADVANCED_CONTROLS_BUTTON_STABLE_SELF_UPDATE:
			result = LauncherMainWindow::ADVANCED_EVENT_FORCE_LAUNCHER_UPDATE;
			break;

		default:
			throw std::runtime_error("Invalid button");
		}

		EndModal(result);
	}

	void AdvancedOptionsWindow::Build() {
		wxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(mainSizer);

		wxButton* selfUpdateButton = new wxButton(this, ADVANCED_CONTROLS_BUTTON_STABLE_SELF_UPDATE,
			"Update the launcher (force)");
		mainSizer->Add(selfUpdateButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

		std::ostringstream str;
		str << "Update the Repentogon installation (force, ";
		if (_mainFrame->GetRepentogonUnstableUpdatesState()) {
			str << "unstable";
		} else {
			str << "stable";
		}
		str << " version)";
		wxButton* updateButton = new wxButton(this, ADVANCED_CONTROLS_BUTTON_FORCE_UPDATE,
			str.str());
		mainSizer->Add(updateButton, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 5);

		Fit();
	}
}