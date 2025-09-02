#include <WinSock2.h>

#include "launcher/windows/launch_countdown.h"
#include "launcher/windows/launcher.h"

wxBEGIN_EVENT_TABLE(Launcher::LaunchCountdownWindow, wxDialog)
// EVT_CHAR_HOOK(Launcher::LaunchCountdownWindow::OnKeyPressed)
EVT_BUTTON(Launcher::LaunchCountdownWindow::Events::EVENT_CANCEL_BUTTON, Launcher::LaunchCountdownWindow::OnCancelButtonClick)
EVT_TIMER(Launcher::LaunchCountdownWindow::Events::EVENT_TIMER, Launcher::LaunchCountdownWindow::OnProgressTimer)
EVT_UPDATE_UI(wxID_ANY, Launcher::LaunchCountdownWindow::OnUpdate)
wxEND_EVENT_TABLE()

namespace Launcher {
	LaunchCountdownWindow::LaunchCountdownWindow(LauncherMainWindow* mainFrame) : _mainFrame(mainFrame), wxDialog(mainFrame, -1, "REPENTOGON Launcher") {
		Build();
	}

	LaunchCountdownWindow::~LaunchCountdownWindow() {
		if (_timer) {
			_timer->Stop();
			wxTheApp->ScheduleForDestruction(_timer);
		}
	}

	// void LaunchCountdownWindow::OnKeyPressed(wxKeyEvent& event) {
	// 	if (event.GetKeyCode() == 'r' || event.GetKeyCode() == 'R') {
	// 		EndModal(wxID_CANCEL);
	// 	}
	// }

	void LaunchCountdownWindow::OnCancelButtonClick(wxCommandEvent& event) {
		EndModal(wxID_CANCEL);
	}

	void LaunchCountdownWindow::OnProgressTimer(wxTimerEvent& event) {
		if (--_countdownSeconds <= 0) {
			EndModal(wxID_OK);
		} else {
			UpdateText();
		}
	}

	void LaunchCountdownWindow::OnUpdate(wxUpdateUIEvent& event) {
		SetFocus();
	}

	void LaunchCountdownWindow::UpdateText() {
		if (_text) {
			_text->SetLabel(wxString::Format("Launching Isaac in %d...", _countdownSeconds));
		}
	}

	void LaunchCountdownWindow::Build() {
		wxPanel* panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(300, 60));
		panel->SetBackgroundColour(*wxWHITE);
		wxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
		_countdownSeconds = 3;
		_text = new wxStaticText(panel, wxID_ANY, "", wxPoint(10, 10));
		UpdateText();
		panelSizer->Add(_text, 0, wxALL, 20);
		panel->SetSizer(panelSizer);

		wxPanel* panel2 = new wxPanel(this, wxID_ANY, wxPoint(0, 60), wxSize(300, 35));
		_cancelButton = new wxButton(panel2, Events::EVENT_CANCEL_BUTTON, "Open Launcher", wxPoint(195, 5));
		wxSizer* panelSizer2 = new wxBoxSizer(wxHORIZONTAL);
		panelSizer2->Add(_cancelButton);
		panel2->SetSizer(panelSizer2);

		_timer = new wxTimer(this, Events::EVENT_TIMER);
		_timer->Start(1000);  // 1 second in milliseconds
		
		Fit();
		CenterOnScreen();
	}
}
