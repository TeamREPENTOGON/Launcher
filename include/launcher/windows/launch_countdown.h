#pragma once

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif


namespace Launcher {
	class LauncherMainWindow;

	class LaunchCountdownWindow : public wxDialog {
	public:
		enum Events {
			EVENT_TIMER,
			EVENT_CANCEL_BUTTON,
		};

		LaunchCountdownWindow(LauncherMainWindow* mainFrame);
		~LaunchCountdownWindow();

	private:
		// void OnKeyPressed(wxKeyEvent& event);
		void OnCancelButtonClick(wxCommandEvent& event);
		void OnProgressTimer(wxTimerEvent& event);
		void OnUpdate(wxUpdateUIEvent& event);

		void UpdateText();
		void Build();

	private:
		LauncherMainWindow* _mainFrame;
		wxTimer* _timer;
		wxStaticText* _text;
		wxButton* _cancelButton;
		int _countdownSeconds = -1;

		DECLARE_EVENT_TABLE();
	};
}
