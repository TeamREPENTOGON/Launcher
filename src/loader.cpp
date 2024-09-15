#include <Windows.h>

#include <cstdio>
#include <ctime>

#include <vector>

#include "launcher/loader.h"

#define INIT_STR(KEY, VALUE) strcpy(stringTable[KEY], VALUE)

void LoaderData::InitializeStringTable() {
	INIT_STR(STRING_LAUNCHER, "zhlLoader.dll");
	INIT_STR(STRING_LAUNCH, "Launch");
}

#define STR(NAME) env->stringTable[NAME]

#pragma code_seg(push, r1, ".trampo")
/* Disable all checks that could lead to the function containing information
 * it shouldn't: preservation of esp across function calls, balancing of the
 * stack etc.
 *
 * safebuffers disables sanity checks which are address space dependant.
 *
 * Because the functions are injected into the game, their environement is limited
 * to their stack frame. In particular they cannot call any function that
 * would cause the linker (be it static or dynamic) to be invoked, they
 * cannot reference any variable outside their own stack frame, and they cannot
 * use read-only strings.
 *
 * Calls to functions within the same section are okay because the compiler
 * will use a relative call when translating the call, and the offset of the call
 * is preserved when the section is copied.
 */
#pragma check_stack(off)
#pragma runtime_checks("", off)
/* param is a pointer to a LoaderData structure injected into the memory of
 * the game by the launcher. This structure contains pointers to the functions
 * in kernel32.dll that are required to load and initialize ZHL, as well as
 * all ZHL mods. This works because kernel32.dll is mapped at the same address
 * in all virtual address spaces due to legacy constraints.
 *
 * The function returns one of the LoadDllResult values.
 */
__declspec(safebuffers) static DWORD WINAPI LoadDLLs(LPVOID param) {
	LoaderData* env = (LoaderData*)param;
	HMODULE launcher = env->loadLibraryA(STR(STRING_LAUNCHER));
	if (!launcher) {
		return LOAD_DLL_ERR_NO_LAUNCHER;
	}

	int(*init)(bool) = (int(*)(bool))env->getProcAddress(launcher, STR(STRING_LAUNCH));
	if (!init) {
		env->freeLibrary(launcher);
		return LOAD_DLL_ERR_NO_LAUNCH;
	}

	if (init(env->withConsole)) {
		env->freeLibrary(launcher);
		return LOAD_DLL_ERR_LAUNCH;
	}

	return LOAD_DLL_OK;
}
#pragma code_seg(pop, r1)

#pragma check_stack
#pragma runtime_checks("", restore)

void* GetLoadDLLsAddress() {
	return &LoadDLLs;
}