#include <Windows.h>

#include "comm/messages.h"
#include "shared/logger.h"
#include "self_updater/synchronization.h"

namespace Synchronization {
	enum IOResult {
		IO_RESULT_OK,
		IO_RESULT_ERR,
		IO_RESULT_ERR_TIMEOUT,
		IO_RESULT_ERR_INVALID_CALL,
		IO_RESULT_ERR_INCONSISTENT_TRANSFER
	};

	static IOResult ReadAndWait(HANDLE pipe, DWORD timeout, void* buffer, size_t size);
	static IOResult WriteAndWait(const void* buffer, size_t size, HANDLE pipe, DWORD timeout);
	static IOResult WaitOverlapped(HANDLE pipe, OVERLAPPED* overlapped, DWORD timeout, DWORD* nTransferred);

	SynchronizationResult SynchronizeWithLauncher() {
		HANDLE pipe = CreateFileA(Comm::PipeName, GENERIC_READ | GENERIC_WRITE, 0,
			NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
		if (pipe == INVALID_HANDLE_VALUE) {
			return SYNCHRONIZATION_ERR_CANNOT_OPEN_PIPE;
		}

		if (IOResult res = WriteAndWait(Comm::UpdaterHello, strlen(Comm::UpdaterHello), pipe, 2000); res != IO_RESULT_OK) {
			Logger::Error("SynchronizeWithLauncher: error while sending hello to launcher (%d)\n", res);
			CloseHandle(pipe);
			return SYNCHRONIZATION_ERR_SEND_PING;
		}

		char buffer[100] = { 0 };
		if (IOResult res = ReadAndWait(pipe, 2000, buffer, strlen(Comm::LauncherHello)); res != IO_RESULT_OK) {
			Logger::Error("SynchronizeWithLauncher: error while receiving hello from launcher (%d)\n", res);
			CloseHandle(pipe);
			return SYNCHRONIZATION_ERR_RECV_PONG;
		}

		buffer[strlen(Comm::LauncherHello)] = '\0';
		if (strcmp(buffer, Comm::LauncherHello)) {
			Logger::Error("SynchronizeWithLauncher: incorrect ack received from launcher (expected %s, got %s)\n",
				Comm::LauncherHello, buffer);
			CloseHandle(pipe);
			return SYNCHRONIZATION_ERR_INVALID_PONG;
		}

		if (IOResult res = WriteAndWait(Comm::UpdaterRequestPID, strlen(Comm::UpdaterRequestPID), pipe, 2000); res != IO_RESULT_OK) {
			Logger::Error("SynchronizeWithLauncher: error while sending launcher pid request (%d)\n", res);
			CloseHandle(pipe);
			return SYNCHRONIZATION_ERR_SEND_PID_REQUEST;
		}

		memset(buffer, 0, sizeof(buffer));
		if (IOResult res = ReadAndWait(pipe, 2000, buffer, strlen(Comm::LauncherAnswerPID)); res != IO_RESULT_OK) {
			Logger::Error("SynchronizeWithLauncher: error while receiving launcher pid ack (%d)\n", res);
			CloseHandle(pipe);
			return SYNCHRONIZATION_ERR_RECV_PID_ANSWER;
		}

		buffer[strlen(Comm::LauncherAnswerPID)] = '\0';
		if (strcmp(buffer, Comm::LauncherAnswerPID)) {
			Logger::Error("SynchronizeWithLauncher: incorrect launcher pid ack received from launcher (expected %s, got %s)\n",
				Comm::LauncherAnswerPID, buffer);
			CloseHandle(pipe);
			return SYNCHRONIZATION_ERR_INVALID_RECV_PID;
		}

		DWORD pid;
		if (IOResult res = ReadAndWait(pipe, 2000, &pid, sizeof(pid)); res != IO_RESULT_OK) {
			Logger::Error("SynchronizeWithLauncher: error while receiving launcher pid (%d)\n", res);
			CloseHandle(pipe);
			return SYNCHRONIZATION_ERR_RECV_PID;
		}

		HANDLE launcher = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
		if (launcher == NULL) {
			Logger::Error("SynchronizeWithLauncher: failed to open launcher process (%d)\n", GetLastError());
			CloseHandle(pipe);
			return SYNCHRONIZATION_ERR_OPEN_PROCESS;
		}

		BOOL terminated = TerminateProcess(launcher, 0);
		if (!terminated) {
			Logger::Error("SynchronizeWithLauncher: failed to terminate launcher process (%d)\n", GetLastError());
			CloseHandle(launcher);
			CloseHandle(pipe);
			return SYNCHRONIZATION_ERR_TERMINATE_PROCESS;
		}

		DWORD waitResult = WaitForSingleObject(launcher, 5000);
		if (waitResult != WAIT_OBJECT_0) {
			Logger::Error("SynchronizeWithLauncher: error while waiting for launcher to complete (%d)\n", waitResult);
			CloseHandle(launcher);
			CloseHandle(pipe);
			return SYNCHRONIZATION_ERR_WAIT_PROCESS;
		}

		CloseHandle(launcher);
		CloseHandle(pipe);
		return SYNCHRONIZATION_OK;
	}

	IOResult WriteAndWait(const void* buffer, size_t size, HANDLE pipe, DWORD timeout) {
		OVERLAPPED overlapped;
		memset(&overlapped, 0, sizeof(overlapped));

		DWORD nWritten = 0;
		BOOL result = WriteFile(pipe, buffer, size, NULL, &overlapped);
		if (result == FALSE) {
			if (GetLastError() == ERROR_IO_PENDING) {
				if (IOResult ioResult = WaitOverlapped(pipe, &overlapped, timeout, &nWritten); ioResult != IO_RESULT_OK) {
					return ioResult;
				}
			}
			else {
				return IO_RESULT_ERR;
			}
		}

		if (size != nWritten) {
			return IO_RESULT_ERR_INCONSISTENT_TRANSFER;
		}

		return IO_RESULT_OK;
	}

	IOResult ReadAndWait(HANDLE pipe, DWORD timeout, void* buffer, size_t size) {
		OVERLAPPED overlapped;
		memset(&overlapped, 0, sizeof(overlapped));

		DWORD nRead = 0;
		BOOL result = ReadFile(pipe, buffer, size, NULL, &overlapped);

		if (result == FALSE) {
			if (GetLastError() == ERROR_IO_PENDING) {
				if (IOResult ioResult = WaitOverlapped(pipe, &overlapped, timeout, &nRead); ioResult != IO_RESULT_OK) {
					return ioResult;
				}
			}
			else {
				return IO_RESULT_ERR;
			}
		}

		if (nRead != size) {
			return IO_RESULT_ERR_INCONSISTENT_TRANSFER;
		}

		return IO_RESULT_OK;
	}

	IOResult WaitOverlapped(HANDLE pipe, OVERLAPPED* overlapped, DWORD timeout, DWORD* nTransferred) {
		BOOL result = GetOverlappedResultEx(pipe, overlapped, nTransferred, timeout, FALSE);

		if (result == FALSE) {
			DWORD err = GetLastError();
			if (err == ERROR_IO_INCOMPLETE || err == WAIT_IO_COMPLETION) {
				return IO_RESULT_ERR_INVALID_CALL;
			}
			else if (err == WAIT_TIMEOUT) {
				return IO_RESULT_ERR_TIMEOUT;
			}
		}

		return IO_RESULT_OK;
	}
}