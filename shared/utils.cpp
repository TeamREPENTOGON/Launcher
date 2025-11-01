#include "shared/status_codes.h"
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

		case GUARD_PAGE_VIOLATION:
			str = "guard page violation";
			break;

		case DATATYPE_MISALIGNMENT:
			str = "data type misalignment";
			break;

		case BREAKPOINT:
			str = "breakpoint";
			break;

		case SINGLE_STEP:
			str = "single step";
			break;

		case LONGJUMP:
			str = "longjump";
			break;

		case UNWIND_CONSOLIDATE:
			str = "unwind consolidate";
			break;

		case ACCESS_VIOLATION:
			str = "access violation";
			break;

		case IN_PAGE_ERROR:
			str = "in page error";
			break;

		case INVALID_HANDLE:
			str = "invalid handle";
			break;

		case INVALID_PARAMETER:
			str = "invalid parameter";
			break;

		case NO_MEMORY:
			str = "no memory";
			break;

		case ILLEGAL_INSTRUCTION:
			str = "illegal instruction";
			break;

		case NONCONTINUABLE_EXCEPTION:
			str = "non continuable exception";
			break;

		case INVALID_DISPOSITION:
			str = "invalid disposition";
			break;

		case ARRAY_BOUNDS_EXCEEDED:
			str = "array bounds exceeded";
			break;

		case FLOAT_DENORMAL_OPERAND:
			str = "float denormal operand";
			break;

		case FLOAT_DIVIDE_BY_ZERO:
			str = "float divide by zero";
			break;

		case FLOAT_INEXACT_RESULT:
			str = "float inexact result";
			break;

		case FLOAT_INVALID_OPERATION:
			str = "float invalid operation";
			break;

		case FLOAT_OVERFLOW:
			str = "float overflow";
			break;

		case FLOAT_STACK_CHECK:
			str = "float stack check";
			break;

		case FLOAT_UNDERFLOW:
			str = "float underflow";
			break;

		case INTEGER_DIVIDE_BY_ZERO:
			str = "integer divide by zero";
			break;

		case INTEGER_OVERFLOW:
			str = "integer overflow";
			break;

		case PRIVILEGED_INSTRUCTION:
			str = "privileged instruction";
			break;

		case STACK_OVERFLOW:
			str = "stack overflow";
			break;

		case DLL_NOT_FOUND:
			str = "dll not found";
			break;

		case ORDINAL_NOT_FOUND:
			str = "ordinal not found";
			break;

		case ENTRYPOINT_NOT_FOUND:
			str = "entrypoint not found";
			break;

		case CONTROL_C_EXIT:
			break;

		case DLL_INIT_FAILED:
			str = "dll init failed";
			break;

		case CONTROL_STACK_VIOLATION:
			str = "control stack violation";
			break;

		case FLOAT_MULTIPLE_FAULTS:
			str = "float multiple faults";
			break;

		case FLOAT_MULTIPLE_TRAPS:
			str = "float multiple traps";
			break;

		case REG_NAT_CONSUMPTION:
			str = "reg nat consumption (seriously how the fuck did you trigger this ?)";
			break;

		case HEAP_CORRUPTION:
			str = "heap corruption";
			break;

		case STACK_BUFFER_OVERRUN:
			str = "stack buffer overrun";
			break;

		case INVALID_CRUNTIME_PARAMETER:
			str = "invalid cruntime parameter";
			break;

		case ASSERTION_FAILURE:
			str = "assertion failure";
			break;

		case ENCLAVE_VIOLATION:
			str = "enclave violation";
			break;

		case INTERRUPTED:
			str = "interrupted";
			break;

		case THREAD_NOT_RUNNING:
			str = "thread not running";
			break;

		case ALREADY_REGISTERED:
			str = "already registered";
			break;

		default:
			str = "unknown error code";
			break;
		}
	}
}
