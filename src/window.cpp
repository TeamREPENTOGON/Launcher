#include <cstdio>
#include <regex>

#include "inih/ini.h"
#include "inih/cpp/INIReader.h"

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"

#include "curl/curl.h"
#include "launcher/window.h"

wxBEGIN_EVENT_TABLE(IsaacLauncher::MainFrame, wxFrame)
	EVT_COMBOBOX(IsaacLauncher::WINDOW_COMBOBOX_LEVEL, IsaacLauncher::MainFrame::OnLevelSelect)
	EVT_COMBOBOX(IsaacLauncher::WINDOW_COMBOBOX_LAUNCH_MODE, IsaacLauncher::MainFrame::OnLauchModeSelect)
	EVT_CHECKBOX(IsaacLauncher::WINDOW_CHECKBOX_REPENTOGON_CONSOLE, IsaacLauncher::MainFrame::OnOptionSelected)
	EVT_CHECKBOX(IsaacLauncher::WINDOW_CHECKBOX_REPENTOGON_UPDATES, IsaacLauncher::MainFrame::OnOptionSelected)
	EVT_CHECKBOX(IsaacLauncher::WINDOW_CHECKBOX_VANILLA_LUADEBUG, IsaacLauncher::MainFrame::OnOptionSelected)
	EVT_TEXT(IsaacLauncher::WINDOW_TEXT_VANILLA_LUAHEAPSIZE, IsaacLauncher::MainFrame::OnCharacterWritten)
	EVT_BUTTON(IsaacLauncher::WINDOW_BUTTON_LAUNCH_BUTTON, IsaacLauncher::MainFrame::Launch)
wxEND_EVENT_TABLE()

namespace IsaacLauncher {
	static std::tuple<wxFont, wxFont> MakeBoldFont(wxFrame* frame);
	static wxComboBox* CreateLevelsComboBox(wxFrame* frame);

	/* RAII-style class to automatically clean the CURL session. */
	class ScopedCURL {
	public:
		ScopedCURL(CURL* curl) : _curl(curl) { }
		~ScopedCURL() { curl_easy_cleanup(_curl); }

	private:
		CURL* _curl;
	};

	namespace Defaults {
		bool console = false;
		int levelStage = 0;
		int stageType = 0;
		bool luaDebug = false;
		int luaHeapSize = 1024;
	}

	namespace Sections {
		std::string repentogon("Repentogon");
		std::string vanilla("Vanilla");
		std::string shared("Shared");
	}

	namespace Keys {
		std::string console("Console");
		std::string levelStage("LevelStage");
		std::string luaDebug("LuaDebug");
		std::string luaHeapSize("LuaHeapSize");
		std::string stageType("StageType");
		std::string launchMode("LaunchMode");
	}

	namespace IsaacInterface {
		// LevelStage
		static constexpr unsigned int STAGE_NULL = 0;
		static constexpr unsigned int STAGE4_1 = 7;
		static constexpr unsigned int STAGE4_2 = 8;
		static constexpr unsigned int STAGE4_3 = 9;
		static constexpr unsigned int STAGE8 = 13;
		static constexpr unsigned int NUM_STAGES = 14;

		// StageType
		static constexpr unsigned int STAGETYPE_ORIGINAL = 0;
		static constexpr unsigned int STAGETYPE_GREEDMODE = 3;
		static constexpr unsigned int STAGETYPE_REPENTANCE_B = 5;
	}

	const char* levelNames[] = {
			"Basement",
			"Cellar",
			"Burning Basement",
			"Downpour",
			"Dross",
			"Caves",
			"Catacombs",
			"Flooded Caves",
			"Mines",
			"Ashpit",
			"Depths",
			"Necropolis",
			"Dank Depths",
			"Mausoleum",
			"Gehenna",
			"Womb",
			"Utero",
			"Scarred Womb",
			"Corpse",
			NULL
	};

	static const char* uniqueLevelNames[] = {
		"??? (9.0)",
		"Sheol (10.0)",
		"Cathedral (10.1)",
		"Dark Room (11.0)",
		"Chest (11.1)",
		"The Void (12.0)",
		"Home (13.0)",
		NULL
	};

	static const char* mandatoryFileNames[] = {
		"libzhl.dll",
		"zhlRepentogon.dll",
		"freetype.dll",
		NULL
	};

	Version const knownVersions[] = {
		{ "04469d0c3d3581936fcf85bea5f9f4f3a65b2ccf96b36310456c9626bac36dc6", "v1.7.9b.J835 (Steam)", true },
		{ NULL, NULL, false }
	};

	Version const* const validVersions[] = {
		NULL
	};

	/* Function called by cURL when it gets a response from a request. 
	 * userp is a pointer to a std::string.
	 */
	static size_t OnCurlResponse(void* contents, size_t size, size_t nmemb, void* userp) {
		std::string* result = (std::string*)userp;
		result->append((char*)contents, nmemb * size);
		return nmemb * size;
	}

	Version const* MainFrame::GetVersion(const char* hash) {
		Version const* version = knownVersions;
		while (version->version) {
			if (!strcmp(hash, version->hash)) {
				return version;
			}

			++version;
		}

		return NULL;
	}

	bool MainFrame::FileExists(const char* name) {
		WIN32_FIND_DATAA data;
		memset(&data, 0, sizeof(data));
		HANDLE result = FindFirstFileA(name, &data);
		bool ret = result != INVALID_HANDLE_VALUE;
		if (ret) {
			FindClose(result);
		}

		return ret;
	}

	MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "REPENTOGON Launcher") {
		memset(&_options, 0, sizeof(_options));
		_grid = new wxGridBagSizer(0, 20);
		_console = nullptr;
		_luaHeapSize = nullptr;
		_enabledRepentogon = false;
		_repentogonVersion = nullptr;

		SetSize(1024, 650);

		AddLaunchOptions();
		AddRepentogonOptions();
		AddVanillaOptions();

		wxSizer* horizontalSizer = new wxBoxSizer(wxHORIZONTAL);
		horizontalSizer->Add(_grid, 0, wxLEFT, 20);

		wxSizer* verticalSizer = new wxBoxSizer(wxVERTICAL);
		wxTextCtrl* logWindow = new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, 400), wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH);
		logWindow->SetBackgroundColour(*wxWHITE);
		_logWindow = logWindow;
		verticalSizer->Add(logWindow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 20);
		verticalSizer->Add(horizontalSizer);
		SetSizer(verticalSizer);

		SetBackgroundColour(wxColour(237, 237, 237));
	}

	void MainFrame::AddLaunchOptions() {
		auto [sourceFont, boldFont] = MakeBoldFont(this);
		boldFont.SetPointSize(14);
		SetFont(boldFont);
		wxStaticText* text = new wxStaticText(this, -1, "Launch mode");
		SetFont(sourceFont);

		wxSizer* box = new wxBoxSizer(wxHORIZONTAL);
		box->Add(new wxStaticText(this, -1, "Launch mode: "));

		_launchMode = new wxComboBox(this, WINDOW_COMBOBOX_LAUNCH_MODE);
		_launchMode->Insert("Repentogon", 0, (void*)nullptr);
		_launchMode->Insert("Vanilla", 0, (void*)nullptr);
		_launchMode->SetValue("Repentogon");

		box->Add(_launchMode);

		_grid->Add(text, wxGBPosition(0, 2), wxDefaultSpan, wxALIGN_CENTER);
		_grid->Add(box, wxGBPosition(1, 2));

		wxButton* launchButton = new wxButton(this, WINDOW_BUTTON_LAUNCH_BUTTON, "Launch game");
		_grid->Add(launchButton, wxGBPosition(2, 2), wxDefaultSpan, wxEXPAND);
	}

	void MainFrame::AddRepentogonOptions() {
		auto [sourceFont, boldFont] = MakeBoldFont(this);
		boldFont.SetPointSize(14);
		SetFont(boldFont);
		wxStaticText* text = new wxStaticText(this, -1, "REPENTOGON Options");
		SetFont(sourceFont);

		// wxCheckBox* updates = new wxCheckBox(this, WINDOW_CHECKBOX_REPENTOGON_UPDATES, "Check for updates");
		// updates->SetValue(true);
		wxCheckBox* console = new wxCheckBox(this, WINDOW_CHECKBOX_REPENTOGON_CONSOLE, "Enable console window");
		console->SetValue(false);

		// _updates = updates;
		_console = console;

		_grid->Add(text, wxGBPosition(0, 0), wxDefaultSpan, wxALIGN_CENTER);
		// _grid->Add(updates, wxGBPosition(1, 0));
		_grid->Add(console, wxGBPosition(1, 0));
	}

	void MainFrame::AddVanillaOptions() {
		auto [sourceFont, boldFont] = MakeBoldFont(this);
		boldFont.SetPointSize(14);
		SetFont(boldFont);
		wxStaticText* text = new wxStaticText(this, -1, "Universal Options");
		SetFont(sourceFont);

		wxSizer* levelSelectSizer = new wxBoxSizer(wxHORIZONTAL);
		levelSelectSizer->Add(new wxStaticText(this, -1, "Starting stage: "));
		_levelSelect = CreateLevelsComboBox(this);
		levelSelectSizer->Add(_levelSelect);

		_grid->Add(text, wxGBPosition(0, 1), wxDefaultSpan, wxALIGN_CENTER);
		_grid->Add(levelSelectSizer, wxGBPosition(1, 1));
		_luaDebug = new wxCheckBox(this, WINDOW_CHECKBOX_VANILLA_LUADEBUG, "Enable luadebug (unsafe)");
		_grid->Add(_luaDebug, wxGBPosition(2, 1));

		wxSizer* heapSizeBox = new wxBoxSizer(wxHORIZONTAL);
		wxTextValidator heapSizeValidator(wxFILTER_NUMERIC);
		wxTextCtrl* heapSizeCtrl = new wxTextCtrl(this, WINDOW_TEXT_VANILLA_LUAHEAPSIZE, "1024");
		heapSizeCtrl->SetValidator(heapSizeValidator);
		_luaHeapSize = heapSizeCtrl;
		wxStaticText* heapSizeText = new wxStaticText(this, -1, "Lua heap size (MB): ");
		heapSizeBox->Add(heapSizeText);
		heapSizeBox->Add(heapSizeCtrl);
		_grid->Add(heapSizeBox, wxGBPosition(3, 1));
	}

	void MainFrame::OnLevelSelect(wxCommandEvent& event) {
		wxComboBox* box = dynamic_cast<wxComboBox*>(event.GetEventObject());
		wxString string = box->GetValue();
		std::cmatch match;
		const char* text = string.c_str().AsChar();
		if (std::regex_search(text, match, std::basic_regex("([0-9])\\.([0-9])"))) {
			int stage = std::stoi(match[1].str(), NULL, 0);
			int type = std::stoi(match[2].str(), NULL, 0);
			_options.level_stage = stage;
			_options.stage_type = type;
		}
		else if (!strcmp(text, "--")) {
			_options.level_stage = _options.stage_type = 0;
		}
	}

	void MainFrame::OnLauchModeSelect(wxCommandEvent& event) {
		wxComboBox* box = dynamic_cast<wxComboBox*>(event.GetEventObject());
		if (box->GetValue() == "Vanilla") {
			_options.mode = LAUNCH_MODE_VANILLA;
		}
		else {
			_options.mode = LAUNCH_MODE_REPENTOGON;
		}

		UpdateRepentogonOptionsFromLaunchMode();
	}

	void MainFrame::OnCharacterWritten(wxCommandEvent& event) {
		wxTextCtrl* ctrl = dynamic_cast<wxTextCtrl*>(event.GetEventObject());
		_options.lua_heap_size = std::stoi(ctrl->GetValue().c_str().AsChar());
	}

	void MainFrame::OnOptionSelected(wxCommandEvent& event) {
		wxCheckBox* box = dynamic_cast<wxCheckBox*>(event.GetEventObject());
		switch (box->GetId()) {
		case WINDOW_CHECKBOX_REPENTOGON_CONSOLE:
			_options.console = box->GetValue();
			break;

		case WINDOW_CHECKBOX_REPENTOGON_UPDATES:
			// _options.updates = box->GetValue();
			break;

		case WINDOW_CHECKBOX_VANILLA_LUADEBUG:
			_options.lua_debug = box->GetValue();
			break;

		default:
			return;
		}
	}

	void MainFrame::Launch(wxCommandEvent& event) {
		Log("Launching the game with the following options:");
		Log("\tRepentogon:");
		if (_options.mode == LAUNCH_MODE_VANILLA) {
			Log("\t\tRepentogon is disabled");
		}
		else {
			Log("\t\tEnable Repentogon console window: %s", _options.console ? "yes" : "no");
		}
		Log("\tVanilla:");
		if (_options.level_stage) {
			Log("\t\tStarting stage: %d.%d", _options.level_stage, _options.stage_type);
		}
		else {
			Log("\t\tStarting stage: not selected");
		}
		Log("\t\tLua debug: %s", _options.lua_debug ? "yes" : "no");
		Log("\t\tLua heap size: %dM", _options.lua_heap_size);

		WriteLauncherConfiguration();
	}

	void MainFrame::PostInit() {
		Log("Welcome to the REPENTOGON Launcher version %s", IsaacLauncher::version);
		CheckVersionsAndInstallation();
		InitializeOptions();
	}

	bool MainFrame::CheckIsaacVersion() {
		Log("Checking Isaac version...");
		WIN32_FIND_DATAA isaac;
		memset(&isaac, 0, sizeof(isaac));
		HANDLE isaacFile = FindFirstFileA("isaac-ng.exe", &isaac);
		if (isaacFile == INVALID_HANDLE_VALUE) {
			LogError("Unable to find isaac-ng.exe");
			return false;
		}
		FindClose(isaacFile);

		DWORD size = isaac.nFileSizeLow;
		char* content = (char*)malloc(size + 2);
		if (!content) {
			LogError("Unable to allocate memory to read game's executable, aborting");
			return false;
		}

		FILE* exe = fopen(isaac.cFileName, "rb");
		fread(content, 1, size, exe);
		content[size] = '\0';
		/* std::string sha256Str;
		CryptoPP::SHA256 sha256;
		CryptoPP::StringSource(content, true,
			new CryptoPP::HashFilter(sha256,
				new CryptoPP::HexEncoder(
					new CryptoPP::StringSink(sha256Str)
				)
			)
		); */
		BCRYPT_ALG_HANDLE alg;
		NTSTATUS err = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
		DWORD buffSize;
		DWORD dummy;
		err = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (unsigned char*)&buffSize, sizeof(buffSize), &dummy, 0);
		BCRYPT_HASH_HANDLE hashHandle;
		unsigned char* hashBuffer = (unsigned char*)malloc(buffSize);
		err = BCryptCreateHash(alg, &hashHandle, hashBuffer, buffSize, NULL, 0, 0);
		err = BCryptHashData(hashHandle, (unsigned char*)content, size, 0);
		DWORD hashSize;
		err = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (unsigned char*)&hashSize, sizeof(hashSize), &dummy, 0);
		unsigned char* hash = (unsigned char*)malloc(hashSize);
		char* hashHex = (char*)malloc(hashSize * 2 + 1);
		err = BCryptFinishHash(hashHandle, hash, hashSize, 0);
		free(hashBuffer);
		err = BCryptCloseAlgorithmProvider(&alg, 0);

		for (int i = 0; i < hashSize; ++i) {
			sprintf(hashHex + 2 * i, "%02hhx", hash[i]);
		}

		// const char* sha256p = sha256Str.c_str();
		Log("\tFound isaac-ng.exe. Hash: %s", hashHex);
		Version const* version = GetVersion(hashHex);
		if (!version) {
			LogError("Unknown version. REPENTOGON will not launch.");
			return false;
		}
		else if (!version->valid) {
			LogError("This version of the game does not support REPENTOGON.");
			return false;
		}

		free(hash);

		Log("\tIdentified REPENTOGON compatible version %s", version->version);

		return true;
	}

	bool MainFrame::CheckRepentogonInstallation() {
		Log("Checking Repentogon installation...");
		const char** file = mandatoryFileNames;
		bool ok = true;
		while (*file) {
			if (FileExists(*file)) {
				Log("\t%s: found", *file);
			}
			else {
				ok = false;
				LogError("\t%s: not found", *file);
			}

			++file;
		}

		if (FileExists("dsound.dll")) {
			wxMessageDialog dialog(NULL, wxT("Found dsound.dll from a previous Repentogon version. This file needs to be deleted. Do you want the launcher to do it?"),
				"Warning", wxYES_NO | wxYES_DEFAULT | wxICON_WARNING);
			// Weirdly, if these methods are not called, the constructor alone is not enough to set the style and title.
			// dialog.SetTitle("Warning");
			// dialog.SetMessageDialogStyle(wxYES_NO | wxYES_DEFAULT | wxICON_WARNING);

			int result = dialog.ShowModal();
			if (result == wxID_YES) {
				DeleteFileA("dsound.dll");
			}
			else {
				LogWarn("Found dsound.dll from a previous Repentogon version. Keeping this file may cause Repentogon to crash");
			}
		}

		return ok;
	}

	bool MainFrame::CheckRepentogonVersion() {
		Log("Checking Repentogon version...");
		HMODULE repentogon = LoadLibraryExA("zhlRepentogon.dll", NULL, DONT_RESOLVE_DLL_REFERENCES);
		if (!repentogon) {
			LogError("Unable to open zhlRepentogon.dll");
			return false;
		}

		HMODULE zhl = LoadLibraryExA("libzhl.dll", NULL, DONT_RESOLVE_DLL_REFERENCES);
		if (!zhl) {
			FreeLibrary(repentogon);
			LogError("Unable to open libzhl.dll");
			return false;
		}

		bool result = false;
		FARPROC repentogonVersion = GetProcAddress(repentogon, "__REPENTOGON_VERSION");
		FARPROC zhlVersion = GetProcAddress(zhl, "__ZHL_VERSION");

		const char* repentogonVersionStr = nullptr;
		const char* zhlVersionStr = nullptr;

		if (!repentogonVersion) {
			LogError("Unable to get Repentogon's version (%d)", GetLastError());
			result = false;
			goto end;
		}

		if (!zhlVersion) {
			LogError("Unable to get ZHL's version");
			result = false;
			goto end;
		}

		repentogonVersionStr = *(const char**)repentogonVersion;
		zhlVersionStr = *(const char**)zhlVersion;

		Log("\tFound Repentogon version %s", repentogonVersionStr);
		Log("\tFound ZHL version %s", zhlVersionStr);

		if (strcmp(repentogonVersionStr, zhlVersionStr)) {
			LogError("Repentogon/ZHL version mismatch");
			result = false;
			goto end;
		}

		Log("\tRepentogon and ZHL versions match");

		_repentogonVersion = (char*)malloc(strlen(zhlVersionStr) + 1);
		if (!_repentogonVersion) {
			LogError("Unable to allocate memory to store version of Repentogon. Update checking will not work");
		}
		else {
			strcpy(_repentogonVersion, zhlVersionStr);
		}
		result = true;

	end:
		FreeLibrary(repentogon);
		FreeLibrary(zhl);
		return result;
	}

	void MainFrame::Log(const char* fmt, ...) {
		char buffer[4096];
		va_list va;
		va_start(va, fmt);
		int count = vsnprintf(buffer, 4096, fmt, va);
		va_end(va);

		if (count == 0)
			return;

		wxString text(buffer);
		if (buffer[count] != '\n')
			text += '\n';
		_logWindow->AppendText(text);
	}

	void MainFrame::LogWarn(const char* fmt, ...) {
		char buffer[4096];
		va_list va;
		va_start(va, fmt);
		int count = vsnprintf(buffer, 4096, fmt, va);
		va_end(va);

		if (count == 0)
			return;

		wxString text(buffer);
		text.Prepend("[WARN] ");
		if (buffer[count] != '\n')
			text += '\n';

		wxTextAttr attr = _logWindow->GetDefaultStyle();
		wxColour color;
		color.Set(235, 119, 52);
		_logWindow->SetDefaultStyle(wxTextAttr(color));
		_logWindow->AppendText(text);
		_logWindow->SetDefaultStyle(attr);
	}

	void MainFrame::LogError(const char* fmt, ...) {
		char buffer[4096];
		va_list va;
		va_start(va, fmt);
		int count = vsnprintf(buffer, 4096, fmt, va);
		va_end(va);

		if (count == 0)
			return;

		wxString text(buffer);
		text.Prepend("[ERROR] ");
		if (buffer[count] != '\n')
			text += '\n';
		wxTextAttr attr = _logWindow->GetDefaultStyle();
		_logWindow->SetDefaultStyle(wxTextAttr(*wxRED));
		_logWindow->AppendText(text);
		_logWindow->SetDefaultStyle(attr);
	}

	std::tuple<wxFont, wxFont> MakeBoldFont(wxFrame* frame) {
		wxFont source = frame->GetFont();
		wxFont bold = source;
		bold.MakeBold();
		return std::make_tuple(source, bold);
	}

	wxComboBox* CreateLevelsComboBox(wxFrame* frame) {
		wxComboBox* box = new wxComboBox(frame, WINDOW_COMBOBOX_LEVEL, "Start level");

		int pos = 0;
		box->Insert(wxString("--"), pos++);
		box->SetValue("--");
		int variant = 0;
		for (const char* name : levelNames) {
			if (!name)
				continue;

			wxString s;
			int level = 1 + 2 * (variant / 5);
			box->Insert(s.Format("%s I (%d.%d)", name, level, variant % 5), pos++, (void*)nullptr);
			box->Insert(s.Format("%s II (%d.%d)", name, level + 1, variant % 5), pos++, (void*)nullptr);

			++variant;
		}

		for (const char* name : uniqueLevelNames) {
			if (!name)
				continue;
			box->Insert(wxString(name), pos++, (void*)nullptr);
		}

		return box;
	}

	void MainFrame::CheckVersionsAndInstallation() {
		if (!CheckIsaacVersion()) {
			return;
		}

		if (!CheckRepentogonInstallation()) {
			return;
		}

		if (!CheckRepentogonVersion()) {
			return;
		}

		_enabledRepentogon = true;
	}

	void MainFrame::InitializeOptions() {
		INIReader reader(std::string("launcher.ini"));
		if (reader.ParseError() == -1) {
			Log("No configuration file found, using defaults");
			_options.console = Defaults::console;
			_options.level_stage = Defaults::levelStage;
			_options.lua_debug = Defaults::luaDebug;
			_options.lua_heap_size = Defaults::luaHeapSize;
			_options.mode = _enabledRepentogon ? LAUNCH_MODE_REPENTOGON : LAUNCH_MODE_VANILLA;
			_options.stage_type = Defaults::stageType;
		}
		else {
			Log("Found configuration file launcher.ini");
			_options.console = reader.GetBoolean(Sections::repentogon, Keys::console, Defaults::console);
			_options.level_stage = reader.GetInteger(Sections::vanilla, Keys::levelStage, Defaults::levelStage);
			_options.lua_debug = reader.GetBoolean(Sections::vanilla, Keys::luaDebug, Defaults::luaDebug);
			_options.lua_heap_size = reader.GetInteger(Sections::vanilla, Keys::luaHeapSize, Defaults::luaHeapSize);
			_options.stage_type = reader.GetInteger(Sections::vanilla, Keys::stageType, Defaults::stageType);
			_options.mode = (LaunchMode)reader.GetInteger(Sections::shared, Keys::launchMode, LAUNCH_MODE_REPENTOGON);

			if (_options.mode != LAUNCH_MODE_REPENTOGON && _options.mode != LAUNCH_MODE_VANILLA) {
				LogWarn("Invalid value %d for %s field in launcher.ini. Overriding with default", _options.mode, Keys::launchMode);
				if (_enabledRepentogon) {
					_options.mode = LAUNCH_MODE_REPENTOGON;
				}
				else {
					_options.mode = LAUNCH_MODE_VANILLA;
				}
			}

			if (_options.lua_heap_size < 0) {
				LogWarn("Invalid value %d for %s field in launcher.ini. Overriding with default", _options.lua_heap_size, Keys::luaHeapSize.c_str());
				_options.lua_heap_size = Defaults::luaHeapSize;
			}

			if (_options.level_stage < IsaacInterface::STAGE_NULL || _options.level_stage > IsaacInterface::STAGE8) {
				LogWarn("Invalid value %d for %s field in launcher.ini. Overriding with default", _options.level_stage, Keys::levelStage.c_str());
				_options.level_stage = Defaults::levelStage;
				_options.stage_type = Defaults::stageType;
			}

			if (_options.stage_type < IsaacInterface::STAGETYPE_ORIGINAL || _options.stage_type > IsaacInterface::STAGETYPE_REPENTANCE_B) {
				LogWarn("Invalid value %d for %s field in launcher.ini. Overriding with default", _options.stage_type, Keys::stageType.c_str());
				_options.stage_type = Defaults::stageType;
			}

			if (_options.stage_type == IsaacInterface::STAGETYPE_GREEDMODE) {
				LogWarn("Value 3 (Greed mode) for %s field in launcher.ini is deprecated since Repentance."
						"Overriding with default", Keys::stageType.c_str());
				_options.stage_type = Defaults::stageType;
			}

			// Validate stage type for Chapter 4.
			if (_options.level_stage == IsaacInterface::STAGE4_1 || _options.level_stage == IsaacInterface::STAGE4_2) {
				if (_options.stage_type == IsaacInterface::STAGETYPE_REPENTANCE_B) {
					LogWarn("Invalid value %d for %s field associated with value %d "
							"for %s field in launcher.ini. Overriding with default", _options.level_stage, Keys::levelStage.c_str(),
							_options.stage_type, Keys::stageType.c_str());
					_options.stage_type = Defaults::stageType;
				}
			}

			// Validate stage type for Chapters > 4.
			if (_options.level_stage >= IsaacInterface::STAGE4_3 && _options.level_stage <= IsaacInterface::STAGE8) {
				if (_options.stage_type != IsaacInterface::STAGETYPE_ORIGINAL) {
					LogWarn("Invalid value %d for %s field associated with value %d "
							"for %s field in launcher.ini. Overriding with default", _options.level_stage, Keys::levelStage.c_str(),
							_options.stage_type, Keys::stageType.c_str());
					_options.stage_type = Defaults::stageType;
				}
			}
		}
		
		InitializeGUIFromOptions();
	}

	void MainFrame::InitializeGUIFromOptions() {
		_console->SetValue(_options.console);

		// stringstream sucks
		char buffer[11];
		sprintf(buffer, "%d", _options.lua_heap_size);
		_luaHeapSize->SetValue(wxString(buffer));

		_luaDebug->SetValue(_options.lua_debug);
		InitializeLevelSelectFromOptions();
		if (_options.mode == LAUNCH_MODE_REPENTOGON) {
			_launchMode->SetValue("Repentogon");
		}
		else {
			_launchMode->SetValue("Vanilla");
		}

		UpdateRepentogonOptionsFromLaunchMode();
	}

	void MainFrame::InitializeLevelSelectFromOptions() {
		int level = _options.level_stage, type = _options.stage_type;
		std::string value;
		if (level == 0) {
			value = "--";
		}
		else {
			std::string levelName;
			if (level <= IsaacInterface::STAGE4_2) {
				/* To find the level name, divide level by two to get the chapter - 1, then add the stage type. */
				levelName = levelNames[((level - 1) / 2) * 5 + type];
				if (level % 2 == 0) {
					levelName += " II";
				}
				else {
					levelName += " I";
				}

				char buffer[10];
				sprintf(buffer, " (%d.%d)", level, type);
				levelName += std::string(buffer);
			}
			else {
				levelName = uniqueLevelNames[level - IsaacInterface::STAGE4_3];
			}
			value = levelName;
		}

		_levelSelect->SetValue(wxString(value));
	}

	void MainFrame::UpdateRepentogonOptionsFromLaunchMode() {
		if (_launchMode->GetValue() == "Repentogon") {
			_console->Enable(true);
		}
		else {
			_console->Enable(false);
		}
	}

	void MainFrame::WriteLauncherConfiguration() {
		FILE* f = fopen("launcher.ini", "w");
		if (!f) {
			LogError("Unable to open file to write launcher configuration, skpping");
			return;
		}

		fprintf(f, "[%s]\n", Sections::repentogon.c_str());
		fprintf(f, "%s = %d\n", Keys::console.c_str(), _options.console);

		fprintf(f, "[%s]\n", Sections::vanilla.c_str());
		fprintf(f, "%s = %d\n", Keys::levelStage.c_str(), _options.level_stage);
		fprintf(f, "%s = %d\n", Keys::stageType.c_str(), _options.stage_type);
		fprintf(f, "%s = %d\n", Keys::luaHeapSize.c_str(), _options.lua_heap_size);
		fprintf(f, "%s = %d\n", Keys::luaDebug.c_str(), _options.lua_debug);
		
		fprintf(f, "[%s]\n", Sections::shared.c_str());
		fprintf(f, "%s = %d\n", Keys::launchMode.c_str(), _options.mode);

		fclose(f);
	}

	bool MainFrame::CheckRepentogonUpdates() {
		return CheckUpdates("https://api.github.com/repos/TeamREPENTOGON/REPENTOGON/releases/latest", _repentogonVersion);
	}

	bool MainFrame::CheckSelfUpdates() {
		return CheckUpdates("", "dev");
	}

	bool MainFrame::CheckUpdates(const char* url, const char* currentVersion) {
		if (!strcmp(currentVersion, "nightly") || !strcmp(currentVersion, "dev")) {
			Log("Skipping updates checks: nightly version detected");
		}

		Log("Checking updates of %s at %s. Currently installed version is %s", _currentUpdate.c_str(), url, currentVersion);
		CURL* curl;
		CURLcode curlResult; 

		curl = curl_easy_init();
		if (!curl) {
			LogError("Error while initializing cURL session", _currentUpdate.c_str());
			return false;
		}
		else {
			Log("\tInitialized cURL session");
		}

		ScopedCURL session(curl);

		struct curl_slist* curlHeaders = NULL;
		const char* headers[] = {
			"Accept: application/vnd.github+json",
			"X-GitHub-Api-Version: 2022-11-28",
			"User-Agent: REPENTOGON",
			NULL
		};

		for (const char* header : headers) {
			curlHeaders = curl_slist_append(curlHeaders, header);
		}

		std::string response;
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnCurlResponse);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlHeaders);

		curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, 5);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);

		Log("\tPerforming HTTP request at %s", url);
		curlResult = curl_easy_perform(curl);
		if (curlResult != CURLE_OK) {
			LogError("Error while performing HTTP request to retrieve version information about %s\ncURL error: %s", _currentUpdate.c_str(), curl_easy_strerror(curlResult));
			return false;
		}

		Log("\tSucessfully received HTTP response");

		rapidjson::Document doc;
		doc.Parse(response.c_str());

		if (doc.HasParseError()) {
			LogError("Error while parsing HTTP response. rapidjson error code %d", doc.GetParseError());
			return false;
		}

		if (!doc.HasMember("name")) {
			LogError("Malformed HTTP response: no field called \"name\"");
			return false;
		}

		const char* remoteVersion = doc["name"].GetString();
		if (strcmp(remoteVersion, currentVersion)) {
			Log("\tNew version available: %s", remoteVersion);
			return true;
		}
		else {
			Log("\t%s is up-to-date", _currentUpdate.c_str());
			return false;
		}
	}
}