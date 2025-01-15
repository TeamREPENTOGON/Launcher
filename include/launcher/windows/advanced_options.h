#pragma once

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif


namespace Launcher {
	class MainFrame;

	class AdvancedOptionsWindow : public wxDialog {
	public:
		enum Controls {
			ADVANCED_CONTROLS_BUTTON_FORCE_UPDATE,
			ADVANCED_CONTROLS_BUTTON_FORCE_UNSTABLE_UPDATE,
			ADVANCED_CONTROLS_BUTTON_STABLE_SELF_UPDATE,
			ADVANCED_CONTROLS_BUTTON_UNSTABLE_SELF_UPDATE
		};

		AdvancedOptionsWindow(MainFrame* mainFrame);
		~AdvancedOptionsWindow();

	private:
		void Build();
		void OnButtonSelect(wxCommandEvent&);

	private:
		MainFrame* _mainFrame;

		DECLARE_EVENT_TABLE();
	};
}