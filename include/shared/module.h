#pragma once

#include <Windows.h>

#include <string>

namespace Module {
    enum ValidateStringSymbolResult {
        VALIDATE_STRING_SYMBOL_MODULE_INFORMATION,
        VALIDATE_STRING_SYMBOL_OOB

    };

    bool ValidateStringSymbol(HMODULE library, FARPROC addr, std::string& value,
        ValidateStringSymbolResult* result);
}