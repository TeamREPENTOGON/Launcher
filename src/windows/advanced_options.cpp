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
EVT_BUTTON(Launcher::AdvancedOptionsWindow::Controls::ADVANCED_CONTROLS_BUTTON_REINSTALL,
	Launcher::AdvancedOptionsWindow::OnButtonSelect)
wxEND_EVENT_TABLE()


wxBEGIN_EVENT_TABLE(Launcher::AdvancedModOptionsWindow, wxDialog)
EVT_TEXT(Launcher::WINDOW_TEXT_VANILLA_LUAHEAPSIZE, Launcher::AdvancedModOptionsWindow::OnLuaHeapSizeCharacterWritten)
wxEND_EVENT_TABLE()

namespace Launcher {
	AdvancedOptionsWindow::AdvancedOptionsWindow(LauncherMainWindow* mainFrame) : _mainFrame(mainFrame), wxDialog(mainFrame, -1, "Update/Repair Instalation") {
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
		case Controls::ADVANCED_CONTROLS_BUTTON_REINSTALL:
			result = LauncherMainWindow::ADVANCED_EVENT_REINSTALL;
			break;
		default:
			throw std::runtime_error("Invalid button");
		}

		EndModal(result);
	}



	AdvancedModOptionsWindow::AdvancedModOptionsWindow(LauncherMainWindow* mainFrame) : _mainFrame(mainFrame), wxDialog(mainFrame, -1, "Advanced Mod options") {
		Build();
	}

	AdvancedModOptionsWindow::~AdvancedModOptionsWindow() {

	}

	void AdvancedOptionsWindow::Build() {
		wxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(mainSizer);

		wxButton* selfUpdateButton = new wxButton(this, ADVANCED_CONTROLS_BUTTON_STABLE_SELF_UPDATE,
			"Update the launcher (force)");
		mainSizer->Add(selfUpdateButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

		std::ostringstream str;
		str << "Update the REPENTOGON installation (force, ";
		if (_mainFrame->GetRepentogonUnstableUpdatesState()) {
			str << "unstable";
		} else {
			str << "stable";
		}
		str << " version)";
		wxButton* updateButton = new wxButton(this, ADVANCED_CONTROLS_BUTTON_FORCE_UPDATE,
			str.str());
		mainSizer->Add(updateButton, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 10);
		
		wxButton* reinstallButton = new wxButton(this, ADVANCED_CONTROLS_BUTTON_REINSTALL,"Re-install/Repair REPENTOGON");
		mainSizer->Add(reinstallButton, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT | wxBOTTOM, 10);

		Fit();
	}



	void AdvancedModOptionsWindow::OnLuaHeapSizeCharacterWritten(wxCommandEvent& event) {
		wxTextCtrl* ctrl = dynamic_cast<wxTextCtrl*>(event.GetEventObject());
		std::regex LuaHeapSizeRegex("^([0-9]+[KMG]{0,1})?$");
		std::string luaHeapSize = ctrl->GetValue().c_str().AsChar();
		std::transform(luaHeapSize.begin(), luaHeapSize.end(), luaHeapSize.begin(), toupper);

		std::cmatch match;
		if (std::regex_match(luaHeapSize.c_str(), match, LuaHeapSizeRegex)) {
			_mainFrame->_configuration->SetLuaHeapSize(luaHeapSize);
		}
		else if (luaHeapSize == "K" || luaHeapSize == "M" || luaHeapSize == "G") {
			_mainFrame->_configuration->SetLuaHeapSize("");
		}
		else {
			wxBell();
			const long ip = ctrl->GetInsertionPoint() - 1;
			ctrl->SetValue(_mainFrame->_configuration->LuaHeapSize());
			ctrl->SetInsertionPoint(ip > 0 ? ip : 0);
		}
	}


	void AdvancedModOptionsWindow::Build() {
		wxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(mainSizer);
		luaDebug = new wxCheckBox(this, Launcher::WINDOW_CHECKBOX_VANILLA_LUADEBUG, "Enable luadebug (unsafe)");
			luaDebug->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
				if (luaDebug->IsChecked()) {
					luaDebug->SetValue(false);
					int res = wxMessageBox(
						"By enabling this, you give all your auto-updating workshop mods FULL UNRESTRICTED ACCESS TO YOUR COMPUTER!!, they can delete files, encrypt your harddrive, mine bitcoins, send out your login tokens, you name it...\nAre you sure you want to enable it?",
						"Enable luadebug",
						wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
						this
					);
					luaDebug->SetValue(res == wxYES);
					_mainFrame->_configuration->SetLuaDebug(res == wxYES);
				}
				else {
					_mainFrame->_configuration->SetLuaDebug(false);
				}
			event.Skip();
				});
			luaDebug->SetValue(_mainFrame->_configuration->LuaDebug());
			mainSizer->Add(luaDebug, 0, wxALL, 10);

			wxSizer* heapSizeBox = new wxBoxSizer(wxHORIZONTAL);
			luaheapSize = new wxTextCtrl(this, WINDOW_TEXT_VANILLA_LUAHEAPSIZE, "1024M");
			wxStaticText* heapSizeText = new wxStaticText(this, -1, "Lua heap size: ");
			luaheapSize->SetValue(_mainFrame->_configuration->LuaHeapSize());
			heapSizeText->SetToolTip("Debug option that allows you to set the amount of memory the game allocates to the Lua engine used by mods.\n\nRealistically, you'll probably never need to change this from the default (1024M).");
			luaheapSize->CopyToolTip(heapSizeText->GetToolTip());
			heapSizeBox->Add(heapSizeText, 0, wxALIGN_CENTER_VERTICAL);
			heapSizeBox->Add(luaheapSize, 0, wxALIGN_CENTER_VERTICAL);

			mainSizer->Add(heapSizeBox, 0, wxBOTTOM | wxLEFT | wxRIGHT, 10);

		Fit();
	}
}

