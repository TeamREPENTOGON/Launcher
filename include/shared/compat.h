#pragma once

#if __cpp_lib_unreachable >= 202202L
#define LAUNCHER_UNREACHABLE std::unreachable()
#else
#define LAUNCHER_UNREACHABLE __assume(false)
#endif