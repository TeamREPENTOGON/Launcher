#include <WinSock2.h>

#include "launcher/windows/checklogs_modal.h"
#include "launcher/windows/launcher.h"

wxBEGIN_EVENT_TABLE(Launcher::CheckLogsWindow, wxDialog)
EVT_BUTTON(Launcher::CheckLogsWindow::Controls::CHECKLOGS_CONTROLS_BUTTON_LAUNCHERLOG,
	Launcher::CheckLogsWindow::OnButtonSelect)
EVT_BUTTON(Launcher::CheckLogsWindow::Controls::CHECKLOGS_CONTROLS_BUTTON_RGONLOG,
	Launcher::CheckLogsWindow::OnButtonSelect)
EVT_BUTTON(Launcher::CheckLogsWindow::Controls::CHECKLOGS_CONTROLS_BUTTON_GAMELOG,
	Launcher::CheckLogsWindow::OnButtonSelect)
wxEND_EVENT_TABLE()

namespace Launcher {
	CheckLogsWindow::CheckLogsWindow(LauncherMainWindow* mainFrame) : _mainFrame(mainFrame), wxDialog(mainFrame, -1, "Open Game Logs") {
		Build();
	}

	CheckLogsWindow::~CheckLogsWindow() {

	}

	void CheckLogsWindow::OnButtonSelect(wxCommandEvent& event) {
		int id = ((wxButton*)event.GetEventObject())->GetId();
		int result = LauncherMainWindow::CHECKLOGS_EVENT_NONE;

		switch (id) {
		case Controls::CHECKLOGS_CONTROLS_BUTTON_LAUNCHERLOG:
			result = LauncherMainWindow::CHECKLOGS_EVENT_LAUNCHERLOG;
			break;

		case Controls::CHECKLOGS_CONTROLS_BUTTON_RGONLOG:
			result = LauncherMainWindow::CHECKLOGS_EVENT_RGONLOG;
			break;
			
		case Controls::CHECKLOGS_CONTROLS_BUTTON_GAMELOG:
			result = LauncherMainWindow::CHECKLOGS_EVENT_GAMELOG;
			break;

		default:
			throw std::runtime_error("Invalid button");
		}

		EndModal(result);
	}

	void CheckLogsWindow::Build() {
		wxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(mainSizer);

		wxButton* Launcherlog = new wxButton(this, Controls::CHECKLOGS_CONTROLS_BUTTON_LAUNCHERLOG,
			"Open Launcher Log File (launcher.log)");
		mainSizer->Add(Launcherlog, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		
		wxButton* RgonLog = new wxButton(this, Controls::CHECKLOGS_CONTROLS_BUTTON_RGONLOG,
			"Open Repentogon Log File (repentogon.log)");
		mainSizer->Add(RgonLog, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		
		wxButton* GameLog = new wxButton(this, Controls::CHECKLOGS_CONTROLS_BUTTON_GAMELOG,
			"Open Main Log File (log.txt)");
		mainSizer->Add(GameLog, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);


		Fit();
	}
}