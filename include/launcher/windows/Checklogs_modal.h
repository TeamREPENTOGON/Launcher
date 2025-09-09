#pragma once

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif


namespace Launcher {
	class LauncherMainWindow;

	class CheckLogsWindow : public wxDialog {
	public:
		enum Controls {
			CHECKLOGS_CONTROLS_BUTTON_LAUNCHERLOG,
			CHECKLOGS_CONTROLS_BUTTON_RGONLOG,
			CHECKLOGS_CONTROLS_BUTTON_GAMELOG
		};

		CheckLogsWindow(LauncherMainWindow* mainFrame);
		~CheckLogsWindow();

	private:
		void Build();
		void OnButtonSelect(wxCommandEvent&);

	private:
		LauncherMainWindow* _mainFrame;

		DECLARE_EVENT_TABLE();
	};
}