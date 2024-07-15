#include <WinSock2.h>
#include <Windows.h>
#include <ImageHlp.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <chrono>

#include <wx/wx.h>

#include "launcher/launcher.h"
#include "launcher/loader.h"
#include "launcher/window.h"

#include "shared/externals.h"
#include "shared/logger.h"

/* Perform the early setup for the injection: create the Isaac process, 
 * allocate memory for the remote thread function etc. 
 * 
 * No extra thread is created in the process. ImGui should be initialized
 * afterwards to setup the injector, and then the remote thread can be
 * created.
 * 
 * Return true of the initialization was sucessful, false otherwise.
 */
static int FirstStageInit(struct IsaacOptions const* options, HANDLE* process, void** page,
	size_t* functionOffset, size_t* paramOffset, PROCESS_INFORMATION* processInfo);

static void GenerateCLI(const struct IsaacOptions* options, char cli[256]);

void GenerateCLI(const struct IsaacOptions* options, char cli[256]) {
	memset(cli, 0, sizeof(cli));
	if (options->console) {
		strcat(cli, "--console ");
	}

	if (options->luaDebug) {
		strcat(cli, "--luadebug ");
	}

	if (options->levelStage) {
		strcat(cli, "--set-stage=");
		char buffer[13]; // 11 chars for a max int (including sign) + 1 char for space + 1 char for '\0'
		sprintf(buffer, "%d ", options->levelStage);
		strcat(cli, buffer);
	}

	if (options->stageType) {
		strcat(cli, "--set-stage-type=");
		char buffer[13]; // 11 chars for a max int (including sign) + 1 char for space + 1 char for '\0'
		sprintf(buffer, "%d ", options->stageType);
		strcat(cli, buffer);
	}

	if (options->luaHeapSize) {
		strcat(cli, "--luaheapsize=");
		strcat(cli, "M");
	}
}

DWORD CreateIsaac(struct IsaacOptions const* options, PROCESS_INFORMATION* processInfo) {
	STARTUPINFOA startupInfo;
	memset(&startupInfo, 0, sizeof(startupInfo));

	memset(processInfo, 0, sizeof(*processInfo));

	char cli[256];
	GenerateCLI(options, cli);

	DWORD result = CreateProcessA("isaac-ng.exe", cli, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startupInfo, processInfo);
	if (result == 0) {
		Logger::Error("Failed to create process: %d\n", GetLastError());
		return -1;
	}
	else {
		Logger::Info("Started isaac-ng.exe in suspended state, processID = %d\n", processInfo->dwProcessId);
	}

	return result;
}

#pragma code_seg(push, r1, ".trampo")
int UpdateMemory(HANDLE process, PROCESS_INFORMATION const* processInfo, void** page, size_t* functionOffset, size_t* paramOffset) {
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
		Logger::Error("Unable to find section \".trampo\"\n");
		return -1;
	}

	HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
	if (!kernel32) {
		Logger::Fatal("Unable to load kernel32.dll\n");
		MessageBoxA(NULL, "Unable to load kernel32.dll", "FATAL ERROR", MB_ICONERROR);
		exit(-1);
	}

	LoaderData data;
#define LOAD_CAST(NAME) (decltype(NAME)*)GetProcAddress(kernel32, #NAME)
	data.freeLibrary = LOAD_CAST(FreeLibrary);
	data.getProcAddress = LOAD_CAST(GetProcAddress);
	data.loadLibraryA = LOAD_CAST(LoadLibraryA);
#undef LOAD_CAST
	data.InitializeStringTable();

	/* Allocate enough space to copy the entire section containing the function 
	 * and add enough room to copy an instance of LoaderData.
	 */
	void* remotePage = VirtualAllocEx(process, NULL, headers->Misc.VirtualSize + sizeof(LoaderData), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!remotePage) {
		Logger::Error("Failed to allocate memory in isaac-ng.exe to load the dsound DLL: %d\n", GetLastError());
		return -1;
	}
	else {
		Logger::Info("Allocated memory for remote thread at %p\n", remotePage);
	}

	SIZE_T bytesWritten = 0;
	BOOL ok = WriteProcessMemory(process, remotePage, (char*)self + headers->VirtualAddress, headers->Misc.VirtualSize, &bytesWritten);
	ok = ok && WriteProcessMemory(process, (char*)remotePage + headers->Misc.VirtualSize, &data, sizeof(data), &bytesWritten);
	if (!ok) {
		Logger::Error("Unable to write content of .trampo into the Isaac process: %d\n", GetLastError());
		VirtualFreeEx(process, remotePage, 0, MEM_RELEASE);
		return -1;
	}

	DWORD dummy;
	ok = VirtualProtectEx(process, NULL, headers->Misc.VirtualSize, PAGE_EXECUTE_READ, &dummy);
	if (!ok) {
		Logger::Warn("Unable to change protection on pages to RX (from RWX): %d\n", GetLastError());
	}

	*page = remotePage;
	/* I want to murder MSVC. Don't put the function in the jump table you fucking stupid dumbass. */
	*functionOffset = (DWORD)GetLoadDLLsAddress() - ((DWORD)self + headers->VirtualAddress);
	*paramOffset = headers->Misc.VirtualSize;
	return 0;
}
#pragma code_seg(pop, r1)

int FirstStageInit(struct IsaacOptions const* options, HANDLE* outProcess, void** page, 
	size_t* functionOffset, size_t* paramOffset, PROCESS_INFORMATION* processInfo) {
	Logger::Info("Starting injector\n");
	DWORD processId = CreateIsaac(options, processInfo);
	
	HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
		FALSE, processInfo->dwProcessId);
	if (!process) {
		Logger::Error("Failed to open process: %d\n", GetLastError());
		return -1;
	}
	else {
		Logger::Info("Acquired handle to isaac-ng.exe, process ID = %d\n", processInfo->dwProcessId);
	}

	if (UpdateMemory(process, processInfo, page, functionOffset, paramOffset)) {
		return -1;
	}

	*outProcess = process;
	
	return 0;
}

int CreateAndWait(HANDLE process, void* remotePage, size_t functionOffset, size_t paramOffset) {
	HANDLE remoteThread = CreateRemoteThread(process, NULL, 0, 
		(LPTHREAD_START_ROUTINE)((char*)remotePage + functionOffset), 
		(char*)remotePage + paramOffset, 0, NULL);
	if (!remoteThread) {
		Logger::Error("Error while creating remote thread: %d\n", GetLastError());
		return -1;
	}
	else {
		Logger::Info("Created remote thread in isaac-ng.exe\n");
	}

	Logger::Info("Waiting for remote thread to complete\n");
	DWORD result = WaitForSingleObject(remoteThread, 60 * 1000);
	switch (result) {
	case WAIT_OBJECT_0:
		Logger::Info("RemoteThread completed\n");
		break;

	case WAIT_ABANDONED:
		Logger::Warn("This shouldn't happened: RemoteThread returned WAIT_ABANDONNED\n");
		return -1;

	case WAIT_TIMEOUT:
		Logger::Error("RemoteThread timed out\n");
		return -1;

	case WAIT_FAILED:
		Logger::Error("WaitForSingleObject on RemoteThread failed: %d\n", GetLastError());
		return -1;
	}

	return 0;
}

int InjectIsaac(int updates, int console, int lua_debug, int level_stage, int stage_type, int lua_heap_size) {
	HANDLE process;
	void* remotePage;
	size_t functionOffset, paramOffset;
	PROCESS_INFORMATION processInfo;

	struct IsaacOptions options;
	options.console = console;
	options.luaDebug = lua_debug;
	options.levelStage = level_stage;
	options.stageType = stage_type;
	options.luaHeapSize = lua_heap_size;

	if (FirstStageInit(&options, &process, &remotePage, &functionOffset, &paramOffset, &processInfo)) {
		return -1;
	}

	if (CreateAndWait(process, remotePage, functionOffset, paramOffset)) {
		return -1;
	}

	DWORD result = ResumeThread(processInfo.hThread);
	if (result == -1) {
		Logger::Error("Failed to resume isaac-ng.exe main thread: %d\n", GetLastError());
		return -1;
	}
	else {
		Logger::Info("Resumed main thread of isaac-ng.exe, previous supend count was %d\n", result);
	}

	Logger::Info("Waiting for isaac-ng.exe main thread to return\n");
	WaitForSingleObject(processInfo.hProcess, INFINITE);
	Logger::Info("isaac-ng.exe completed, shutting down injector\n");

	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);

	return 0;
}

bool Launcher::App::OnInit() {
	Logger::Init();
	Externals::Init();
	MainFrame* frame = new MainFrame();
	frame->Show();
	frame->PostInit();
	return true;
}

void Launch(IsaacOptions const& options) {
	InjectIsaac(options.update, options.console, options.luaDebug,
		options.levelStage, options.stageType, options.luaHeapSize);
}

wxIMPLEMENT_APP(Launcher::App);