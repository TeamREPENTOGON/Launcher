#include <WinSock2.h>
#include <Windows.h>
#include <ImageHlp.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <chrono>
#include <filesystem>

#include <wx/wx.h>

#include "launcher/app.h"
#include "launcher/cli.h"
#include "launcher/diff_patcher.h"
#include "launcher/launcher_configuration.h"
#include "launcher/loader.h"

#include "shared/externals.h"
#include "shared/logger.h"
#include "shared/pe32.h"

/* Perform the early setup for the injection: create the Isaac process,
 * allocate memory for the remote thread function etc.
 *
 * No extra thread is created in the process. ImGui should be initialized
 * afterwards to setup the injector, and then the remote thread can be
 * created.
 *
 * Return true of the initialization was sucessful, false otherwise.
 */
static int FirstStageInit(const char* path, bool isLegacy, LauncherConfiguration const* configuration,
	HANDLE* process, void** page, size_t* functionOffset, size_t* paramOffset,
	PROCESS_INFORMATION* processInfo);

static std::string GenerateCLI(const char* path, LauncherConfiguration const* configuration, bool isLegacy);

std::string GenerateCLI(const char* path, LauncherConfiguration const* configuration, bool isLegacy) {
	std::ostringstream cli;

	cli << "\"" << path << "\"";

	if (configuration->LuaDebug()) {
		cli << " --luadebug";
	}

	if (configuration->Stage() > 0) {
		cli << " --set-stage=" << configuration->Stage() << " --set-stage-type=" << configuration->StageType();
	}

	if (!configuration->LoadRoom().empty()) {
		cli << " --load-room=" << "\"" << configuration->LoadRoom() << "\"";
	}

	if (!configuration->LuaHeapSize().empty()) {
		cli << " --luaheapsize=" << configuration->LuaHeapSize();
	}

	if (isLegacy && configuration->IsaacLaunchMode() == LAUNCH_MODE_VANILLA) {
		cli << " --repentogonoff";
	}

	return cli.str();
}

DWORD CreateIsaac(const char* path, bool isLegacy, LauncherConfiguration const* configuration,
	PROCESS_INFORMATION* processInfo) {
	STARTUPINFOA startupInfo;
	memset(&startupInfo, 0, sizeof(startupInfo));

	memset(processInfo, 0, sizeof(*processInfo));

	const std::string cli = GenerateCLI(path, configuration, isLegacy);

	std::filesystem::path filepath(path);
	std::filesystem::path parentPath = filepath.parent_path();
	std::string parent = parentPath.string();
	const char* currentDirectory = parent.c_str();

	Logger::Info("Creating process with path %s, lpCurrentDirectory = %s\n", path, currentDirectory);
	SetEnvironmentVariableA("LAUNCHED_BY_RGONLAUNCHER", "1");
	DWORD result = CreateProcessA(path, (LPSTR)cli.c_str(), NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, filepath.parent_path().string().c_str(), &startupInfo, processInfo);
	if (result == 0) {
		Logger::Error("Failed to create process: %d\n", GetLastError());
	} else {
		Logger::Info("Started isaac-ng.exe in suspended state, processID = %d\n", processInfo->dwProcessId);
	}

	return result;
}

// #pragma code_seg(push, r1, ".trampo")
int UpdateMemory(LauncherConfiguration const* configuration, HANDLE process,
	PROCESS_INFORMATION const*, void** page, size_t* functionOffset,
	size_t* paramOffset) {
	HMODULE self = GetModuleHandle(NULL);
	PIMAGE_NT_HEADERS ntHeaders = ImageNtHeader(self);
	char* base = (char*)&(ntHeaders->OptionalHeader);
	base += ntHeaders->FileHeader.SizeOfOptionalHeader;
	IMAGE_SECTION_HEADER* headers = (IMAGE_SECTION_HEADER*)base;
	bool found = false;
	for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i) {
		Logger::Info("Header: %s\n", (char*)headers->Name);
		if (!strcmp((char*)headers->Name, ".trampo")) {
			found = true;
			break;
		}
		++headers;
	}

	if (!found) {
		Logger::Error("Unable to find section \".trampo\" in %d headers\n", ntHeaders->FileHeader.NumberOfSections);
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
	data.withConsole = configuration->RepentogonConsole();

	/* Allocate enough space to copy the entire section containing the function
	 * and add enough room to copy an instance of LoaderData.
	 */
	void* remotePage = VirtualAllocEx(process, NULL, headers->Misc.VirtualSize + sizeof(LoaderData), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!remotePage) {
		Logger::Error("Failed to allocate memory in isaac-ng.exe to load the dsound DLL: %d\n", GetLastError());
		return -1;
	} else {
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

	MEMORY_BASIC_INFORMATION info;
	void* baseAddr = NULL;
	ok = VirtualQueryEx(process, remotePage, &info, sizeof(info));
	if (!ok) {
		Logger::Warn("Unable to get information about the allocated pages: %d\n", GetLastError());
	} else {
		baseAddr = info.BaseAddress;
	}

	if (baseAddr != NULL) {
		DWORD dummy;
		ok = VirtualProtectEx(process, baseAddr, headers->Misc.VirtualSize, PAGE_EXECUTE_READ, &dummy);
		if (!ok) {
			Logger::Warn("Unable to change protection on pages to RX (from RWX): %d\n", GetLastError());
		}
	}

	*page = remotePage;
	/* I want to murder MSVC. Don't put the function in the jump table you fucking stupid dumbass. */
	*functionOffset = (DWORD)GetLoadDLLsAddress() - ((DWORD)self + headers->VirtualAddress);
	*paramOffset = headers->Misc.VirtualSize;
	return 0;
}
// #pragma code_seg(pop, r1)

static bool SanitizeRepentogonStartup(const char* path) {
	Logger::Info("Sanitizing Repentogon startup\n");

	try {
		PE32 pe32(path);
		auto [textSectionHeader, _] = pe32.GetSection(".text");
		if (!textSectionHeader) {
			Logger::Error("SanitizeRepentogonStartup: no .text found in %s\n", path);
			return false;
		}

		char signature[sizeof(diff_patcher::ISAAC_MAIN_SIGNATURE)];
		strcpy(signature, diff_patcher::ISAAC_MAIN_SIGNATURE);

		signature[0] = diff_patcher::ISAAC_POISON_BYTE;

		if (!pe32.Lookup(signature, textSectionHeader)) {
			Logger::Warn("SanitizeRepentogonStartup: poisoned main not found in %s, emergency patching\n", path);

			if (!diff_patcher::PatchIsaacMain(path)) {
				Logger::Error("SanitizeRepentogonStartup: unable to poison main in %s\n", path);
				return false;
			}
		}

		return true;
	} catch (std::runtime_error const& e) {
		Logger::Error("Error while sanitizing Repentogon startup: %s\n", e.what());
		return false;
	}
}

int FirstStageInit(const char* path, bool isLegacy, LauncherConfiguration const* configuration,
	HANDLE* outProcess, void** page, size_t* functionOffset, size_t* paramOffset,
	PROCESS_INFORMATION* processInfo) {
	/*
	if (!isLegacy) {
		if (!SanitizeRepentogonStartup(path)) {
			return -1;
		}
	}
	*/
	Logger::Info("Starting injector\n");
	DWORD processId = CreateIsaac(path, isLegacy, configuration, processInfo);
	if (processId == 0) {
		return -1;
	}

	HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
		FALSE, processInfo->dwProcessId);
	if (!process) {
		Logger::Error("Failed to open process: %d\n", GetLastError());
		return -1;
	} else {
		Logger::Info("Acquired handle to isaac-ng.exe, process ID = %d\n", processInfo->dwProcessId);
	}

	if (configuration->IsaacLaunchMode() == LAUNCH_MODE_REPENTOGON) {
		if (UpdateMemory(configuration, process, processInfo, page, functionOffset, paramOffset)) {
			return -1;
		}
	}

	*outProcess = process;

	return 0;
}

static volatile int __trapIsaacLaunch = 1;

int CreateAndWait(HANDLE process, void* remotePage, size_t functionOffset, size_t paramOffset) {
	if (sCLI->TrapIsaacLaunch()) {
		while (__trapIsaacLaunch) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	HANDLE remoteThread = CreateRemoteThread(process, NULL, 0,
		(LPTHREAD_START_ROUTINE)((char*)remotePage + functionOffset),
		(char*)remotePage + paramOffset, 0, NULL);
	if (!remoteThread) {
		Logger::Error("Error while creating remote thread: %d\n", GetLastError());
		return -1;
	} else {
		Logger::Info("Created remote thread in isaac-ng.exe\n");
	}

	Logger::Info("Waiting for remote thread to complete\n");

	DWORD waitTime = sCLI->IsaacWaitTime();
	if (waitTime == 0) {
		waitTime = 600000;
	} else if (waitTime == -1) {
		waitTime = INFINITE;
	}

	DWORD result = WaitForSingleObject(remoteThread, waitTime);
	DWORD threadCode = 0;
	switch (result) {
	case WAIT_OBJECT_0: {
		GetExitCodeThread(remoteThread, &threadCode);
		Logger::Info("RemoteThread completed with exit code %#x\n", threadCode);
		break;
	}

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

	return threadCode;
}

DWORD Launcher::Launch(ILoggableGUI* gui, const char* path, bool isLegacy,
	LauncherConfiguration const* configuration) {
	HANDLE process;
	void* remotePage;
	size_t functionOffset, paramOffset;
	PROCESS_INFORMATION processInfo;

	if (FirstStageInit(path, isLegacy, configuration, &process, &remotePage, &functionOffset, &paramOffset, &processInfo)) {
		gui->LogError("Low level error encountered while starting the game, check log files\n");
		return 0xFFFFFFFF;
	}

	if (configuration->IsaacLaunchMode() == LAUNCH_MODE_REPENTOGON) {
		if (CreateAndWait(process, remotePage, functionOffset, paramOffset)) {
			gui->LogError("Error encountered while injecting Repentogon, check log files\n");
			if (!TerminateProcess(process, 0xFFFFFFFF)) {
				gui->LogError("Unable to kill Isaac process: %d\n", GetLastError());
			}
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			CloseHandle(process);
			return 0xFFFFFFFF;
		}
	}

	DWORD result = ResumeThread(processInfo.hThread);
	if (result == -1) {
		Logger::Error("Failed to resume isaac-ng.exe main thread: %d\n", GetLastError());
		gui->LogError("Game was unable to leave its suspended state, aborting launch\n");
		TerminateProcess(process, 0xFFFFFFFF);
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		CloseHandle(process);
		return 0xFFFFFFFF;
	} else {
		Logger::Info("Resumed main thread of isaac-ng.exe, previous supend count was %d\n", result);
	}

	Logger::Info("Waiting for isaac-ng.exe main thread to return\n");
	WaitForSingleObject(processInfo.hProcess, INFINITE);
	Logger::Info("isaac-ng.exe completed, shutting down injector\n");

	DWORD exitCode = 0;
	BOOL ok = GetExitCodeProcess(processInfo.hProcess, &exitCode);
	if (!ok) {
		Logger::Error("Unable to retrieve exit code\n");
	} else {
		Logger::Info("isaac-ng.exe exited with exit code %#x\n", exitCode);
	}

	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);
	CloseHandle(process);

	return exitCode;
}