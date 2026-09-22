#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include "seh_diagnostics.h"

namespace SafeMemory
{
    template <typename T>
    bool read(std::uintptr_t address, T& value) noexcept
    {
        if (!address)
            return false;

#if defined(CONGLOMERATE_DEBUG_SAFE_MEMORY)
        SIZE_T bytesRead = 0;
        return ReadProcessMemory(
            GetCurrentProcess(),
            reinterpret_cast<const void*>(address),
            &value,
            sizeof(T),
            &bytesRead
        ) != FALSE && bytesRead == sizeof(T);
#else
        __try
        {
            value = *reinterpret_cast<const T*>(address);
            return true;
        }
        __except (SehDiagnostics::handle("safe_memory.read"))
        {
            return false;
        }
#endif
    }

    template <typename T>
    bool write(std::uintptr_t address, const T& value) noexcept
    {
        if (!address)
            return false;

#if defined(CONGLOMERATE_DEBUG_SAFE_MEMORY)
        SIZE_T bytesWritten = 0;
        return WriteProcessMemory(
            GetCurrentProcess(),
            reinterpret_cast<void*>(address),
            &value,
            sizeof(T),
            &bytesWritten
        ) != FALSE && bytesWritten == sizeof(T);
#else
        __try
        {
            *reinterpret_cast<T*>(address) = value;
            return true;
        }
        __except (SehDiagnostics::handle("safe_memory.write"))
        {
            return false;
        }
#endif
    }
}
