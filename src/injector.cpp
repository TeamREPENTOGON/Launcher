#include <WinSock2.h>
#include <Windows.h>
#include <ImageHlp.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include <chrono>

#include <wx/wx.h>

#include "launcher/launcher.h"
#include "launcher/logger.h"
#include "launcher/window.h"

/* Perform the early setup for the injection: create the Isaac process, 
 * allocate memory for the remote thread function etc. 
 * 
 * No extra thread is created in the process. ImGui should be initialized
 * afterwards to setup the injector, and then the remote thread can be
 * created.
 * 
 * Return true of the initialization was sucessful, false otherwise.
 */
static int FirstStageInit(struct IsaacOptions const* options, HANDLE* process, void** page, size_t* functionOffset, PROCESS_INFORMATION* processInfo);

static void Log(const char* fmt, ...);

static void GenerateCLI(const struct IsaacOptions* options, char cli[256]);

void GenerateCLI(const struct IsaacOptions* options, char cli[256]) {
	memset(cli, 0, sizeof(cli));
	if (options->console) {
		strcat(cli, "--console ");
	}

	if (options->lua_debug) {
		strcat(cli, "--luadebug ");
	}

	if (options->level_stage) {
		strcat(cli, "--set-stage=");
		char buffer[13]; // 11 chars for a max int (including sign) + 1 char for space + 1 char for '\0'
		sprintf(buffer, "%d ", options->level_stage);
		strcat(cli, buffer);
	}

	if (options->stage_type) {
		strcat(cli, "--set-stage-type=");
		char buffer[13]; // 11 chars for a max int (including sign) + 1 char for space + 1 char for '\0'
		sprintf(buffer, "%d ", options->stage_type);
		strcat(cli, buffer);
	}

	if (options->lua_heap_size) {
		strcat(cli, "--luaheapsize=");
		strcat(cli, "M");
	}
}

#pragma code_seg(push, r1, ".trampo")
DWORD WINAPI LoadDLLs(LPVOID) {
	HMODULE zhl = LoadLibraryA("libzhl.dll");
	if (!zhl) {
		Log("[ERROR] Unable to load ZHL\n");
		return -1;
	}

	FARPROC init = GetProcAddress(zhl, "InitZHL");
	if (!init) {
		Log("[ERROR] \"Init\" function not found in libzhl.dll\n");
		FreeLibrary(zhl);
		return -1;
	}

	if (init()) {
		Log("[ERROR] Error while loading ZHL. Check zhl.log / libzhl.log\n");
		return -1;
	}

	WIN32_FIND_DATAA data;
	memset(&data, 0, sizeof(data));
	HANDLE files = FindFirstFileA("zhl*.dll", &data);
	BOOL ok = (files != INVALID_HANDLE_VALUE);
	while (ok) {
		if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			HMODULE mod = LoadLibraryA(data.cFileName);
			if (!mod) {
				Log("[ERROR] Error while loading mod %s\n", data.cFileName);
			}
			else {
				init = GetProcAddress(mod, "ModInit");
				if (!init) {
					Log("[ERROR] \"ModInit\" function not found in mod %s\n", data.cFileName);
					FreeLibrary(mod);
				}
				else {
					if (init()) {
						Log("[ERROR] Error while loading mod %s\n", data.cFileName);
						FreeLibrary(mod);
					}
				}
			}
		}
		ok = FindNextFileA(files, &data);
	}

	FindClose(files);
	return 0;
}
#pragma code_seg(pop, r1)

DWORD CreateIsaac(struct IsaacOptions const* options, PROCESS_INFORMATION* processInfo) {
	STARTUPINFOA startupInfo;
	memset(&startupInfo, 0, sizeof(startupInfo));

	memset(processInfo, 0, sizeof(*processInfo));

	char cli[256];
	GenerateCLI(options, cli);

	DWORD result = CreateProcessA("isaac-ng.exe", cli, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startupInfo, processInfo);
	if (result == 0) {
		Log("Failed to create process: %d\n", GetLastError());
		return -1;
	}
	else {
		Log("Started isaac-ng.exe in suspended state, processID = %d\n", processInfo->dwProcessId);
	}

	return result;
}

int UpdateMemory(HANDLE process, PROCESS_INFORMATION const* processInfo, void** page, size_t* functionOffset) {
	HMODULE self = GetModuleHandle(NULL);
	PIMAGE_NT_HEADERS ntHeaders = ImageNtHeader(self);
	char* base = (char*)&(ntHeaders->OptionalHeader);
	base += ntHeaders->FileHeader.SizeOfOptionalHeader;
	IMAGE_SECTION_HEADER* headers = (IMAGE_SECTION_HEADER*)base;
	bool found = false;
	for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i) {
		if (!strcmp((char*)headers->Name, ".trampo")) {
			found = true;
			break;
		}
		++headers;
	}

	if (!found) {
		Log("[ERROR] Unable to find section \".trampo\"\n");
		return -1;
	}

	void* remotePage = VirtualAllocEx(process, NULL, headers->Misc.VirtualSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!remotePage) {
		Log("[ERROR] Failed to allocate memory in isaac-ng.exe to load the dsound DLL: %d\n", GetLastError());
		return -1;
	}
	else {
		Log("[INFO] Allocated memory for remote thread at %p\n", remotePage);
	}

	SIZE_T bytesWritten = 0;
	BOOL ok = WriteProcessMemory(process, remotePage, (char*)self + headers->VirtualAddress, headers->Misc.VirtualSize, &bytesWritten);
	if (!ok) {
		Log("[ERROR] Unable to write content of .trampo into the Isaac process: %d\n", GetLastError());
		VirtualFreeEx(process, remotePage, 0, MEM_RELEASE);
		return -1;
	}

	DWORD dummy;
	ok = VirtualProtectEx(process, NULL, headers->Misc.VirtualSize, PAGE_EXECUTE_READ, &dummy);
	if (!ok) {
		Log("[WARN] Unable to change protection on pages to RX (from RWX): %d\n", GetLastError());
	}

	*page = remotePage;
	*functionOffset = 0;
	return 0;
}

int FirstStageInit(struct IsaacOptions const* options, HANDLE* outProcess, void** page, size_t* functionOffset, PROCESS_INFORMATION* processInfo) {
	{
		FILE* f = fopen("injector.log", "w");
		if (f) {
			fclose(f);
		}
	}

	Log("Starting injector\n");
	DWORD processId = CreateIsaac(options, processInfo);
	
	HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
		FALSE, processInfo->dwProcessId);
	if (!process) {
		Log("Failed to open process: %d\n", GetLastError());
		return -1;
	}
	else {
		Log("Acquired handle to isaac-ng.exe, process ID = %d\n", processInfo->dwProcessId);
	}

	if (UpdateMemory(process, processInfo, page, functionOffset)) {
		return -1;
	}

	*outProcess = process;
	
	return 0;
}

int CreateAndWait(HANDLE process, void* remotePage, size_t functionOffset) {
	HANDLE remoteThread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)((char*)remotePage + functionOffset), remotePage, 0, NULL);
	if (!remoteThread) {
		Log("Error while creating remote thread: %d\n", GetLastError());
		return -1;
	}
	else {
		Log("Created remote thread in isaac-ng.exe\n");
	}

	Log("Waiting for remote thread to complete\n");
	DWORD result = WaitForSingleObject(remoteThread, 60 * 1000);
	switch (result) {
	case WAIT_OBJECT_0:
		Log("RemoteThread completed\n");
		break;

	case WAIT_ABANDONED:
		Log("This shouldn't happened: RemoteThread returned WAIT_ABANDONNED\n");
		return -1;

	case WAIT_TIMEOUT:
		Log("RemoteThread timed out\n");
		return -1;

	case WAIT_FAILED:
		Log("WaitForSingleObject on RemoteThread failed: %d\n", GetLastError());
		return -1;
	}

	return 0;
}

void Log(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);

	FILE* f = fopen("injector.log", "a");
	if (!f) {
		f = stderr;
	}

	char buffer[4096];
	time_t now = time(NULL);
	struct tm* tm = localtime(&now);
	strftime(buffer, 4095, "%Y-%m-%d %H:%M:%S", tm);
	fprintf(f, "[%s] ", buffer);
	vfprintf(f, fmt, va);
	va_end(va);
}

int InjectIsaac(int updates, int console, int lua_debug, int level_stage, int stage_type, int lua_heap_size) {
	HANDLE process;
	void* remotePage;
	size_t functionOffset;
	PROCESS_INFORMATION processInfo;

	struct IsaacOptions options;
	options.console = console;
	options.lua_debug = lua_debug;
	options.level_stage = level_stage;
	options.stage_type = stage_type;
	options.lua_heap_size = lua_heap_size;

	if (FirstStageInit(&options, &process, &remotePage, &functionOffset, &processInfo)) {
		return -1;
	}

	if (CreateAndWait(process, remotePage, functionOffset)) {
		return -1;
	}

	DWORD result = ResumeThread(processInfo.hThread);
	if (result == -1) {
		Log("Failed to resume isaac-ng.exe main thread: %d\n", GetLastError());
		return -1;
	}
	else {
		Log("Resumed main thread of isaac-ng.exe, previous supend count was %d\n", result);
	}

	Log("Waiting for isaac-ng.exe main thread to return\n");
	WaitForSingleObject(processInfo.hProcess, INFINITE);
	Log("isaac-ng.exe completed, shutting down injector\n");

	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);

	return 0;
}

bool IsaacLauncher::App::OnInit() {
	Logger::Init();
	MainFrame* frame = new MainFrame();
	frame->Show();
	frame->PostInit();
	return true;
}

wxIMPLEMENT_APP(IsaacLauncher::App);