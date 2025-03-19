#include "launcher/log_helpers.h"

template<typename... Args>
void Log(wxTextCtrl* ctrl, const char* fmt, Args&&... args) {
	wxString s;
	s.Printf(fmt, std::forward<Args>(args)...);
	ctrl->AppendText(s);
}

template<typename... Args>
void LogError(wxTextCtrl* ctrl, const char* fmt, Args&&... args) {
	wxString s;
	s.Printf(fmt, std::forward<Args>(args)...);
	ctrl->SetForegroundColour(*wxRED);
	ctrl->AppendText(wxString("[ERROR] ") + s);
	ctrl->SetForegroundColour(*wxBLACK);
}

NotificationVisitor::NotificationVisitor(wxTextCtrl* text) : _text(text) {

}

void NotificationVisitor::operator()(Notifications::FileDownload const& download) {
	if (_wasLastDownload && _lastDownloadedId == download._id) {
		long current = _text->GetInsertionPoint();
		long position = current - _lastDownloadedString.size();
		_text->Remove(position, current);
		_text->SetInsertionPoint(position);
	}

	uint32_t& downloadSize = _downloadedSizePerFile[download._id];
	downloadSize += download._size;

	wxString s;
	s.Printf("Downloading file %s (%u bytes)\n", download._filename.c_str(), downloadSize);
	_text->AppendText(s);

	_lastDownloadedString = s;
	_lastDownloadedId = download._id;
    _wasLastDownload = true;
}

void NotificationVisitor::operator()(Notifications::FileRemoval const& removal) {
    _wasLastDownload = false;

	if (removal._ok) {
		Log(_text, "Removing %s\n", removal._name.c_str());
	} else {
		LogError(_text, "Unable to remove %s\n", removal._name.c_str());
	}
}

void NotificationVisitor::operator()(Notifications::GeneralNotification const& general) {
    _wasLastDownload = false;

	if (general._isError) {
		LogError(_text, "%s\n", general._text.c_str());
	} else {
		Log(_text, "%s\n", general._text.c_str());
	}
}

namespace Launcher {

	void DumpRepentogonInstallationState(Launcher::Installation const* installation,
		Launcher::RepentogonInstaller const& installer, wxTextCtrl* text) {
		Launcher::RepentogonInstallationState const& state = installer.GetState();

		using namespace Launcher;

		switch (state.result) {
		case REPENTOGON_INSTALLATION_RESULT_MISSING_ASSET:
			LogError(text, "Could not install Repentogon: bad assets in release information\n");
			LogError(text, "Found hash.txt: %s\n", state.hashOk ? "yes" : "no");
			LogError(text, "Found REPENTOGON.zip: %s\n", state.zipOk ? "yes" : "no");
			break;

		case REPENTOGON_INSTALLATION_RESULT_DOWNLOAD_ERROR:
			LogError(text, "Could not install Repentogon: download error\n");
			LogError(text, "Downloaded hash.txt: %s\n", state.hashFile ? "yes" : "no");
			LogError(text, "Downloaded REPENTOGON.zip: %s\n", state.zipFile ? "yes" : "no");
			break;

		case REPENTOGON_INSTALLATION_RESULT_BAD_HASH:
			LogError(text, "Could not install Repentogon: bad archive hash\n");
			LogError(text, "Expected hash \"%s\", got \"%s\"\n", state.hash.c_str(), state.zipHash.c_str());
			break;

		case REPENTOGON_INSTALLATION_RESULT_EXTRACT_FAILED:
		{
			int i = 0;
			LogError(text, "Could not install Repentogon: error while extracting archive\n");
			for (auto const& [filename, success] : state.unzipedFiles) {
				if (filename.empty()) {
					LogError(text, "Could not extract file %d from the archive\n", i);
				} else {
					LogError(text, "Extracted %s: %s\n", filename.c_str(), success ? "yes" : "no");
				}

				++i;
			}
			break;
		}

		case REPENTOGON_INSTALLATION_RESULT_OK:
			break;

		default:
			LogError(text, "Unknown error code from RepentogonUpdater::UpdateRepentogon: %d\n", state.result);
			break;
		}
	}

	void DisplayRepentogonFilesVersion(Launcher::Installation const* installation,
		int tabs, bool isUpdate, wxTextCtrl* text) {
		for (int i = 0; i < tabs; ++i) {
			Log(text, "\t");
		}

		RepentogonInstallation const& repentogon = installation->GetRepentogonInstallation();

		Log(text, "ZHL version: %s", repentogon.GetZHLVersion().c_str());
		if (isUpdate) {
			Log(text, " (updated from %s)\n", repentogon.GetShadowZHLVersion().c_str());
		} else {
			Log(text, "\n\n");
		}

		for (int i = 0; i < tabs; ++i) {
			Log(text, "\t");
		}

		Log(text, "ZHL loader version: %s", repentogon.GetZHLLoaderVersion().c_str());
		if (isUpdate) {
			Log(text, " (updated from %s)\n", repentogon.GetShadowZHLLoaderVersion().c_str());
		} else {
			Log(text, "\n\n");
		}

		for (int i = 0; i < tabs; ++i) {
			Log(text, "\t");
		}

		Log(text, "Repentogon version: %s", repentogon.GetRepentogonVersion().c_str());
		if (isUpdate) {
			Log(text, " (updated from %s)\n", repentogon.GetShadowRepentogonVersion().c_str());
		} else {
			Log(text, "\n\n");
		}
	}

	void DebugDumpBrokenRepentogonInstallationDLL(wxTextCtrl* text, Launcher::Installation const* installation,
		const char* context, const char* libname, LoadableDlls dll,
		std::string const& (RepentogonInstallation::* ptr)() const, bool* found) {
		RepentogonInstallation const& repentogon = installation->GetRepentogonInstallation();
		Log(text, "\tLoad status of %s (%s): ", context, libname);
		LoadDLLState loadState = repentogon.GetDLLLoadState(dll);

		if (loadState == LOAD_DLL_STATE_OK) {
			std::string const& version = (repentogon.*ptr)();
			if (version.empty()) {
				Log(text, "unable to find version\n");
			} else {
				*found = true;
				Log(text, "found version %s\n", version.c_str());
			}
		} else if (loadState == LOAD_DLL_STATE_FAIL) {
			Log(text, "unable to load\n");
		} else {
			Log(text, "no load attempted\n");
		}
	}

	void DebugDumpBrokenRepentogonInstallation(Launcher::Installation const* installation,
		wxTextCtrl* text) {
		Log(text, "Found no valid installation of Repentogon\n");
		Log(text, "\tRequired files found / not found:\n");
		RepentogonInstallation const& repentogon = installation->GetRepentogonInstallation();
		std::vector<FoundFile> const& files = repentogon.GetRepentogonInstallationFilesState();
		for (FoundFile const& file : files) {
			if (file.found) {
				Log(text, "\t\t%s: found\n", file.filename.c_str());
			} else {
				Log(text, "\t\t%s: not found\n", file.filename.c_str());
			}
		}

		bool zhlVersionAvailable = false, repentogonVersionAvailable = false, zhlLoaderVersionAvailable = false;
		DebugDumpBrokenRepentogonInstallationDLL(text, installation, "the ZHL DLL", Libraries::zhl,
			LOADABLE_DLL_LIBZHL, &RepentogonInstallation::GetZHLVersion, &zhlVersionAvailable);
		DebugDumpBrokenRepentogonInstallationDLL(text, installation, "the ZHL loader DLL", Libraries::loader,
			LOADABLE_DLL_ZHL_LOADER, &RepentogonInstallation::GetZHLLoaderVersion, &zhlLoaderVersionAvailable);
		DebugDumpBrokenRepentogonInstallationDLL(text, installation, "the Repentogon DLL", Libraries::repentogon,
			LOADABLE_DLL_REPENTOGON, &RepentogonInstallation::GetRepentogonVersion, &repentogonVersionAvailable);

		if (zhlVersionAvailable && repentogonVersionAvailable && zhlLoaderVersionAvailable) {
			if (repentogon.RepentogonZHLVersionMatch()) {
				Log(text, "\tZHL / Repentogon version match\n");
			} else {
				Log(text, "\tZHL / Repentogon version mismatch\n");
			}
		}
	}
}