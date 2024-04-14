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

	void InitializeStringTable();
};

void* GetLoadDLLsAddress();