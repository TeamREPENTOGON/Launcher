#include <Windows.h>
#include <psapi.h>

#include "shared/module.h"

namespace Module {
	bool ValidateStringSymbol(HMODULE module, FARPROC symbol, std::string& value,
		ValidateStringSymbolResult* outResult) {
		const char** addr = (const char**)symbol;
		MODULEINFO info;
		DWORD result = GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info));
		if (result == 0) {
			if (outResult)
				*outResult = VALIDATE_STRING_SYMBOL_MODULE_INFORMATION;
			return false;
		}

		/* Start walking the DLL. The walk ends when we find a 0, or when we
		 * would exit the boundaries.
		 */
		const char* base = (const char*)addr;
		const char* limit = (const char*)info.lpBaseOfDll + info.SizeOfImage;

		while (base < limit && *base) {
			++base;
		}

		/* base == limit. The PE format makes it virtually impossible for the
		 * string to end on the very last byte.
		 */
		if (*base) {
			if (outResult)
				*outResult = VALIDATE_STRING_SYMBOL_OOB;
			return false;
		}

		value = *addr;
		return true;
	}
}