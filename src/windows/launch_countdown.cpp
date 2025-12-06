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

	void LaunchCountdownWindow::OnCancelButtonClick(wxCommandEvent&) {
		EndModal(wxID_CANCEL);
	}

	void LaunchCountdownWindow::OnProgressTimer(wxTimerEvent&) {
		if (--_countdownSeconds <= 0) {
			EndModal(wxID_OK);
		} else {
			UpdateText();
		}
	}

	void LaunchCountdownWindow::OnUpdate(wxUpdateUIEvent&) {
		SetFocus();
	}

	void LaunchCountdownWindow::UpdateText() {
		if (_text) {
			_text->SetLabel(wxString::Format("Launching Isaac in %d...", _countdownSeconds));
		}
	}

	void LaunchCountdownWindow::Build() {
		wxPanel* panel = new wxPanel(this);
		panel->SetBackgroundColour(*wxWHITE);

		wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);

		_countdownSeconds = 3;
		_text = new wxStaticText(panel, wxID_ANY, "");
		UpdateText();


		panelSizer->AddStretchSpacer();
		panelSizer->Add(_text, 0, wxALIGN_CENTER | wxALL, 10);
		panelSizer->AddStretchSpacer();

		panel->SetSizer(panelSizer);

		wxPanel* panel2 = new wxPanel(this);

		wxBoxSizer* panelSizer2 = new wxBoxSizer(wxHORIZONTAL);
		_cancelButton = new wxButton(panel2, Events::EVENT_CANCEL_BUTTON,
			"Cancel and Open Launcher");
		panelSizer2->Add(_cancelButton, 1, wxEXPAND | wxALL, 10);
		panel2->SetSizer(panelSizer2);

		wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

		mainSizer->Add(panel, 1, wxEXPAND);
		mainSizer->Add(panel2, 0, wxEXPAND);

		this->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { EndModal(wxID_CANCEL); });
		_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { EndModal(wxID_CANCEL); });
		panel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { EndModal(wxID_CANCEL); });
		panel2->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { EndModal(wxID_CANCEL); });

		SetSizerAndFit(mainSizer);
		CenterOnScreen();

		_timer = new wxTimer(this, Events::EVENT_TIMER);
		_timer->Start(1000); // 1 second
	}
}
