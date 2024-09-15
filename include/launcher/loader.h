#pragma once

enum StringTableIdx {
	STRING_LAUNCHER,
	STRING_LAUNCH,
	STRING_MAX
};

/* The result of loading the DLLs in the memory of Isaac. */
enum LoadDllResult {
	/* Load succeeded. */
	LOAD_DLL_OK,
	/* No launcher found. */
	LOAD_DLL_ERR_NO_LAUNCHER,
	/* Load failed: unable to find Launch. */
	LOAD_DLL_ERR_NO_LAUNCH,
	/* Load failed: error launching. */
	LOAD_DLL_ERR_LAUNCH
};

struct LoaderData {
	decltype(LoadLibraryA)* loadLibraryA;
	decltype(FreeLibrary)* freeLibrary;
	decltype(GetProcAddress)* getProcAddress;
	char stringTable[STRING_MAX][200];
	bool withConsole;

	void InitializeStringTable();
};

/* Return the address of the function used to load the DLLs into the memory
 * of the game.
 * 
 * Said function is a static function defined in loader.cpp. This is done so 
 * that the compiler does not have access to the static address of the 
 * function when translating a call to it, and all calls to the function are
 * resolved at runtime. This solves an issue where the address of the function
 * seen at runtime ends up in the wrong address space.
 */
void* GetLoadDLLsAddress();