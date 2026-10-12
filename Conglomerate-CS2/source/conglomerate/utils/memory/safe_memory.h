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

        __try
        {
            value = *reinterpret_cast<const T*>(address);
            return true;
        }
        __except (SehDiagnostics::handle("safe_memory.read"))
        {
            return false;
        }
    }

    template <typename T>
    bool write(std::uintptr_t address, const T& value) noexcept
    {
        if (!address)
            return false;

        __try
        {
            *reinterpret_cast<T*>(address) = value;
            return true;
        }
        __except (SehDiagnostics::handle("safe_memory.write"))
        {
            return false;
        }
    }
}
