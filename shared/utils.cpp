#include "shared/utils.h"

namespace utils {
    void Tokenize(const char* src, const char* delim, std::vector<std::string>& tokens) {
		const char* start = src;
		const char* token = nullptr;
		while ((token = strpbrk(start, delim))) {
			if (start != token) {
				tokens.emplace_back(start, token);
			}

			start = token + 1;
		}

		if (*(start + 1)) {
			tokens.emplace_back(start);
		}
    }

	void ErrorCodeToString(DWORD code, std::string& str) {
		switch (code) {
		case 0:
			break;

		case STATUS_GUARD_PAGE_VIOLATION:
			str = "guard page violation";
			break;

		case STATUS_DATATYPE_MISALIGNMENT:
			str = "data type misalignment";
			break;

		case STATUS_BREAKPOINT:
			str = "breakpoint";
			break;

		case STATUS_SINGLE_STEP:
			str = "single step";
			break;

		case STATUS_LONGJUMP:
			str = "longjump";
			break;

		case STATUS_UNWIND_CONSOLIDATE:
			str = "unwind consolidate";
			break;

		case STATUS_ACCESS_VIOLATION:
			str = "access violation";
			break;

		case STATUS_IN_PAGE_ERROR:
			str = "in page error";
			break;

		case STATUS_INVALID_HANDLE:
			str = "invalid handle";
			break;

		case STATUS_INVALID_PARAMETER:
			str = "invalid parameter";
			break;

		case STATUS_NO_MEMORY:
			str = "no memory";
			break;

		case STATUS_ILLEGAL_INSTRUCTION:
			str = "illegal instruction";
			break;

		case STATUS_NONCONTINUABLE_EXCEPTION:
			str = "non continuable exception";
			break;

		case STATUS_INVALID_DISPOSITION:
			str = "invalid disposition";
			break;

		case STATUS_ARRAY_BOUNDS_EXCEEDED:
			str = "array bounds exceeded";
			break;

		case STATUS_FLOAT_DENORMAL_OPERAND:
			str = "float denormal operand";
			break;

		case STATUS_FLOAT_DIVIDE_BY_ZERO:
			str = "float divide by zero";
			break;

		case STATUS_FLOAT_INEXACT_RESULT:
			str = "float inexact result";
			break;

		case STATUS_FLOAT_INVALID_OPERATION:
			str = "float invalid operation";
			break;

		case STATUS_FLOAT_OVERFLOW:
			str = "float overflow";
			break;

		case STATUS_FLOAT_STACK_CHECK:
			str = "float stack check";
			break;

		case STATUS_FLOAT_UNDERFLOW:
			str = "float underflow";
			break;

		case STATUS_INTEGER_DIVIDE_BY_ZERO:
			str = "integer divide by zero";
			break;

		case STATUS_INTEGER_OVERFLOW:
			str = "integer overflow";
			break;

		case STATUS_PRIVILEGED_INSTRUCTION:
			str = "privileged instruction";
			break;

		case STATUS_STACK_OVERFLOW:
			str = "stack overflow";
			break;

		case STATUS_DLL_NOT_FOUND:
			str = "dll not found";
			break;

		case STATUS_ORDINAL_NOT_FOUND:
			str = "ordinal not found";
			break;

		case STATUS_ENTRYPOINT_NOT_FOUND:
			str = "entrypoint not found";
			break;

		case STATUS_CONTROL_C_EXIT:
			break;

		case STATUS_DLL_INIT_FAILED:
			str = "dll init failed";
			break;

		case STATUS_CONTROL_STACK_VIOLATION:
			str = "control stack violation";
			break;

		case STATUS_FLOAT_MULTIPLE_FAULTS:
			str = "float multiple faults";
			break;

		case STATUS_FLOAT_MULTIPLE_TRAPS:
			str = "float multiple traps";
			break;

		case STATUS_REG_NAT_CONSUMPTION:
			str = "reg nat consumption (seriously how the fuck did you trigger this ?)";
			break;

		case STATUS_HEAP_CORRUPTION:
			str = "heap corruption";
			break;

		case STATUS_STACK_BUFFER_OVERRUN:
			str = "stack buffer overrun";
			break;

		case STATUS_INVALID_CRUNTIME_PARAMETER:
			str = "invalid cruntime parameter";
			break;

		case STATUS_ASSERTION_FAILURE:
			str = "assertion failure";
			break;

		case STATUS_ENCLAVE_VIOLATION:
			str = "enclave violation";
			break;

		case STATUS_INTERRUPTED:
			str = "interrupted";
			break;

		case STATUS_THREAD_NOT_RUNNING:
			str = "thread not running";
			break;

		case STATUS_ALREADY_REGISTERED:
			str = "already registered";
			break;

		default:
			str = "unknown error code";
			break;
		}
	}
}