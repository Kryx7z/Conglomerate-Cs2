#pragma once

#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace SehDiagnostics
{
    inline int handle(const char* context) noexcept
    {
#if defined(CONGLOMERATE_DEBUG_SAFE_MEMORY)
        static std::atomic<std::uint32_t> emitted{};
        if (emitted.fetch_add(1, std::memory_order_relaxed) < 64u)
        {
            OutputDebugStringA("[Conglomerate][SEH] ");
            OutputDebugStringA(context ? context : "unknown");
            OutputDebugStringA("\n");
        }
#else
        (void)context;
#endif
        return EXCEPTION_EXECUTE_HANDLER;
    }
}
