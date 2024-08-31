#include <WinSock2.h>

#include "launcher/advanced_options_window.h"
#include "launcher/window.h"

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
	AdvancedOptionsWindow::AdvancedOptionsWindow(MainFrame* mainFrame) : _mainFrame(mainFrame), wxDialog(mainFrame, -1, "Advanced options") {
		Build();
	}

	AdvancedOptionsWindow::~AdvancedOptionsWindow() {

	}

	void AdvancedOptionsWindow::OnButtonSelect(wxCommandEvent& event) {
		int id = ((wxButton*)event.GetEventObject())->GetId();
		int result = MainFrame::ADVANCED_EVENT_NONE;

		switch (id) {
		case Controls::ADVANCED_CONTROLS_BUTTON_FORCE_UNSTABLE_UPDATE:
			result = MainFrame::ADVANCED_EVENT_FORCE_REPENTOGON_UNSTABLE_UPDATE;
			break;

		case Controls::ADVANCED_CONTROLS_BUTTON_FORCE_UPDATE:
			result = MainFrame::ADVANCED_EVENT_FORCE_REPENTOGON_UPDATE;
			break;

		case Controls::ADVANCED_CONTROLS_BUTTON_STABLE_SELF_UPDATE:
			result = MainFrame::ADVANCED_EVENT_FORCE_LAUNCHER_UPDATE;
			break;

		case Controls::ADVANCED_CONTROLS_BUTTON_UNSTABLE_SELF_UPDATE:
			result = MainFrame::ADVANCED_EVENT_FORCE_LAUNCHER_UNSTABLE_UPDATE;
			break;

		default:
			throw std::runtime_error("Invalid button");
		}

		EndModal(result);
	}

	void AdvancedOptionsWindow::Build() {
		wxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
		SetSizer(mainSizer);

		wxStaticBox* launcherOptions = new wxStaticBox(this, -1, "Launcher options");
		wxSizer* launcherSizer = new wxStaticBoxSizer(launcherOptions, wxVERTICAL);
		// launcherOptions->SetSizer(launcherSizer);

		wxStaticBox* repentogonOptions = new wxStaticBox(this, -1, "REPENTOGON options");
		wxSizer* repentogonSizer = new wxStaticBoxSizer(repentogonOptions, wxVERTICAL);
		// repentogonOptions->SetSizer(repentogonSizer);

		// wxButton* dummy = new wxButton(launcherOptions, -1);
		// dummy->Hide();
		// launcherSizer->Add(dummy, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);

		wxButton* selfUpdateButton = new wxButton(launcherOptions, ADVANCED_CONTROLS_BUTTON_STABLE_SELF_UPDATE, 
			"Self-update (force, stable version)");
		launcherSizer->Add(selfUpdateButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

		wxButton* unstableSelfUpdateButton = new wxButton(launcherOptions, ADVANCED_CONTROLS_BUTTON_UNSTABLE_SELF_UPDATE, 
			"Self-update (force, unstable version)");
		launcherSizer->Add(unstableSelfUpdateButton, 0, wxEXPAND | wxLEFT | wxBOTTOM | wxRIGHT, 5);

		wxButton* updateButton = new wxButton(repentogonOptions, ADVANCED_CONTROLS_BUTTON_FORCE_UPDATE, 
			"Update Repentogon (force, stable version)");
		repentogonSizer->Add(updateButton, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 5);

		wxButton* unstableUpdateButton = new wxButton(repentogonOptions, ADVANCED_CONTROLS_BUTTON_FORCE_UNSTABLE_UPDATE, 
			"Update Repentogon(force, unstable version)");
		repentogonSizer->Add(unstableUpdateButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

		mainSizer->Add(launcherSizer, 0, wxEXPAND | wxTOP | wxLEFT | wxBOTTOM | wxRIGHT, 10);
		mainSizer->Add(repentogonSizer, 0, wxEXPAND | wxTOP | wxLEFT | wxBOTTOM | wxRIGHT, 10);

		Fit();
	}
}