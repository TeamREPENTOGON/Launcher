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
	
EVT_BUTTON(Launcher::CheckLogsWindow::Controls::CHECKLOGS_CONTROLS_BUTTON_LAUNCHERLOG_LOCATE,
	Launcher::CheckLogsWindow::OnButtonSelect)
EVT_BUTTON(Launcher::CheckLogsWindow::Controls::CHECKLOGS_CONTROLS_BUTTON_RGONLOG_LOCATE,
	Launcher::CheckLogsWindow::OnButtonSelect)
EVT_BUTTON(Launcher::CheckLogsWindow::Controls::CHECKLOGS_CONTROLS_BUTTON_GAMELOG_LOCATE,
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
		
		case Controls::CHECKLOGS_CONTROLS_BUTTON_LAUNCHERLOG_LOCATE:
			result = LauncherMainWindow::CHECKLOGS_EVENT_LAUNCHERLOG_LOCATE;
			break;

		case Controls::CHECKLOGS_CONTROLS_BUTTON_RGONLOG_LOCATE:
			result = LauncherMainWindow::CHECKLOGS_EVENT_RGONLOG_LOCATE;
			break;
			
		case Controls::CHECKLOGS_CONTROLS_BUTTON_GAMELOG_LOCATE:
			result = LauncherMainWindow::CHECKLOGS_EVENT_GAMELOG_LOCATE;
			break;

		default:
			throw std::runtime_error("Invalid button");
		}

		EndModal(result);
	}

	void CheckLogsWindow::Build() {
		wxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(mainSizer);

		wxStaticText* txt = new wxStaticText(this, wxID_ANY, "Launcher Log File (launcher.log):");
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
		int width, height;
		txt->GetTextExtent("Repentogon Log File (repentogon.log):  ", &width, &height);
		txt->SetMinSize(wxSize(width, height));
		row->Add(txt, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		wxButton* Launcherlog = new wxButton(this, Controls::CHECKLOGS_CONTROLS_BUTTON_LAUNCHERLOG_LOCATE,
			"Locate");
		row->Add(Launcherlog, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		wxButton* Launcherlogcate = new wxButton(this, Controls::CHECKLOGS_CONTROLS_BUTTON_LAUNCHERLOG,
			"Open");
		row->Add(Launcherlogcate, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		mainSizer->Add(row, wxTOP, 10);


		wxStaticText* txt2 = new wxStaticText(this, wxID_ANY, "Repentogon Log File (repentogon.log):");
		wxBoxSizer* row2 = new wxBoxSizer(wxHORIZONTAL);
		txt2->SetMinSize(wxSize(width, height));
		row2->Add(txt2, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		wxButton* RgonLog = new wxButton(this, Controls::CHECKLOGS_CONTROLS_BUTTON_RGONLOG_LOCATE,
			"Locate");
		row2->Add(RgonLog, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		wxButton* Rgonlogcate = new wxButton(this, Controls::CHECKLOGS_CONTROLS_BUTTON_RGONLOG,
			"Open");
		row2->Add(Rgonlogcate, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		mainSizer->Add(row2, wxTOP, 10);


		wxStaticText* txt3 = new wxStaticText(this, wxID_ANY, "Main Log File (log.txt):");
		wxBoxSizer* row3 = new wxBoxSizer(wxHORIZONTAL);
		txt3->SetMinSize(wxSize(width, height));
		row3->Add(txt3, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		wxButton* GameLog = new wxButton(this, Controls::CHECKLOGS_CONTROLS_BUTTON_GAMELOG_LOCATE,
			"Locate");
		row3->Add(GameLog, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		wxButton* Gamelogcate = new wxButton(this, Controls::CHECKLOGS_CONTROLS_BUTTON_GAMELOG,
			"Open");
		row3->Add(Gamelogcate, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);
		mainSizer->Add(row3, wxTOP, 10);
		mainSizer->AddSpacer(10);

		Fit();
	}
}