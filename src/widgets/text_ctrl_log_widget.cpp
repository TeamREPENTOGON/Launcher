#include "launcher/widgets/text_ctrl_log_widget.h"

wxTextCtrlLog::wxTextCtrlLog(wxTextCtrl* ctrl) : _control(ctrl) {

}

void wxTextCtrlLog::Log(const char* prefix, bool nl, const char* fmt, ...) {
	char buffer[4096] = { 0 };
	size_t prefixLen = strlen(prefix);
	strncpy(buffer, prefix, 4096);

	{
		wxString text(buffer);
		std::unique_lock<std::mutex> lck(_logMutex);
		_control->AppendText(text);

		memset(buffer, 0, sizeof(buffer));
	}

	va_list va;
	va_start(va, fmt);
	int count = vsnprintf(buffer, 4096, fmt, va);
	va_end(va);

	if (count <= 0)
		return;

	wxString text(buffer);
	if (buffer[count - 1] != '\n' && nl)
		text += '\n';

	std::unique_lock<std::mutex> lck(_logMutex);
	_control->AppendText(text);
}

void wxTextCtrlLog::Log(const char* fmt, ...) {
	char buffer[4096];
	va_list va;
	va_start(va, fmt);
	int count = vsnprintf(buffer, 4096, fmt, va);
	va_end(va);

	if (count <= 0)
		return;

	wxString text(buffer);
	if (buffer[count - 1] != '\n')
		text += '\n';

	std::unique_lock<std::mutex> lck(_logMutex);
	_control->AppendText(text);
}

void wxTextCtrlLog::LogNoNL(const char* fmt, ...) {
	char buffer[4096];
	va_list va;
	va_start(va, fmt);
	int count = vsnprintf(buffer, 4096, fmt, va);
	va_end(va);

	if (count <= 0)
		return;

	wxString text(buffer);
	std::unique_lock<std::mutex> lck(_logMutex);
	_control->AppendText(text);
}

void wxTextCtrlLog::LogInfo(const char* fmt, ...) {
	char buffer[4096];
	va_list va;
	va_start(va, fmt);
	int count = vsnprintf(buffer, 4096, fmt, va);
	va_end(va);

	if (count <= 0)
		return;

	wxString text(buffer);
	text.Prepend("[INFO] ");
	if (buffer[count - 1] != '\n')
		text += '\n';

	std::unique_lock<std::mutex> lck(_logMutex);
	_control->AppendText(text);
}

void wxTextCtrlLog::LogWarn(const char* fmt, ...) {
	char buffer[4096];
	va_list va;
	va_start(va, fmt);
	int count = vsnprintf(buffer, 4096, fmt, va);
	va_end(va);

	if (count <= 0)
		return;

	wxString text(buffer);
	text.Prepend("[WARN] ");
	if (buffer[count - 1] != '\n')
		text += '\n';

	wxTextAttr attr = _control->GetDefaultStyle();
	wxColour color;
	color.Set(235, 119, 52);

	std::unique_lock<std::mutex> lck(_logMutex);
	_control->SetDefaultStyle(wxTextAttr(color));
	_control->AppendText(text);
	_control->SetDefaultStyle(attr);
}

void wxTextCtrlLog::LogError(const char* fmt, ...) {
	char buffer[4096];
	va_list va;
	va_start(va, fmt);
	int count = vsnprintf(buffer, 4096, fmt, va);
	va_end(va);

	if (count <= 0)
		return;

	wxString text(buffer);
	text.Prepend("[ERROR] ");
	if (buffer[count - 1] != '\n')
		text += '\n';

	wxTextAttr attr = _control->GetDefaultStyle();
	std::unique_lock<std::mutex> lck(_logMutex);
	_control->SetDefaultStyle(wxTextAttr(*wxRED));
	_control->AppendText(text);
	_control->SetDefaultStyle(attr);
}

wxTextCtrl* wxTextCtrlLog::Get() {
    return _control;
}