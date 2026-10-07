#pragma once

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include "../debug_console.h"

// ong this is some bullshit

namespace SehDiagnostics
{
    inline int handle(const char* context, EXCEPTION_POINTERS* exceptionInfo = nullptr) noexcept
    {
		const char* name = context ? context : "unknown";
		if (exceptionInfo && exceptionInfo->ExceptionRecord)
		{
			const auto* record = exceptionInfo->ExceptionRecord;
			if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
			{
				const char* operation = "unknown";
				switch (record->ExceptionInformation[0])
				{
				case 0: operation = "read"; break;
				case 1: operation = "write"; break;
				case 8: operation = "execute"; break;
				}
				DebugConsole::rateLimited(name,
					"[exception] SEH caught in %s; code=0x%08lX instruction=%p access=%s target=%p",
					name, record->ExceptionCode, record->ExceptionAddress, operation,
					reinterpret_cast<void*>(record->ExceptionInformation[1]));
			}
			else
			{
				DebugConsole::rateLimited(name, "[exception] SEH caught in %s; code=0x%08lX address=%p", name,
					record->ExceptionCode, record->ExceptionAddress);
			}
		}
		else
			DebugConsole::rateLimited(name, "[exception] SEH caught in %s", name);
        return EXCEPTION_EXECUTE_HANDLER;
    }
}
