#include "movement.h"

#include <Windows.h>
#include <cstdint>

#include "../../interfaces/interfaces.h"
#include "../../utils/memory/patternscan/patternscan.h"

namespace
{
	using GetViewAnglesFn = QAngle_t*(__fastcall*)(void*, int);
	using SetViewAnglesFn = void(__fastcall*)(void*, int, QAngle_t*);
	QAngle_t* g_capturedViewAngles = nullptr;
	void* g_capturedInput = nullptr;
	QAngle_t g_savedViewAngles{};

	GetViewAnglesFn resolveGetViewAngles()
	{
		static auto fn = reinterpret_cast<GetViewAnglesFn>(M::FindPattern(
			"client", "4C 8B C1 85 D2 74 08 48 8D 05 ? ? ? ? C3"));
		return fn;
	}

	SetViewAnglesFn resolveSetViewAngles()
	{
		static auto fn = reinterpret_cast<SetViewAnglesFn>(M::FindPattern(
			"client", "85 D2 75 ? 48 63 81"));
		return fn;
	}

	bool captureViewAnglesSafe(void* input, int slot)
	{
		void* activeInput = I::Input ? I::Input : input;
		if (!activeInput)
			return false;

		__try
		{
			const auto fn = resolveGetViewAngles();
			g_capturedViewAngles = fn ? fn(activeInput, 0) : nullptr;
			if (!g_capturedViewAngles)
				return false;

			g_savedViewAngles = *g_capturedViewAngles;
			g_capturedInput = activeInput;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			g_capturedViewAngles = nullptr;
			return false;
		}
	}

	void restoreViewAnglesSafe()
	{
		__try
		{
			const auto setter = resolveSetViewAngles();
			if (setter && g_capturedInput)
				setter(g_capturedInput, 0, &g_savedViewAngles);

			if (g_capturedViewAngles)
				*g_capturedViewAngles = g_savedViewAngles;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
		g_capturedViewAngles = nullptr;
		g_capturedInput = nullptr;
	}
}

void Movement::suppressInput(void* input)
{
    if (!input)
        return;

    constexpr std::uint64_t blockedButtons = (1ULL << 0) | (1ULL << 1) |
        (1ULL << 2) | (1ULL << 5) | (1ULL << 11) | (1ULL << 13);

    const auto base = reinterpret_cast<std::uintptr_t>(input);
    __try
    {
        if (I::InputUsesLegacyLayout)
        {
            *reinterpret_cast<std::uint64_t*>(base + 0x7A0) &= ~blockedButtons;
            *reinterpret_cast<std::uint64_t*>(base + 0x7A8) = 0;
            *reinterpret_cast<std::uint64_t*>(base + 0x7B0) = 0;
            *reinterpret_cast<std::uint64_t*>(base + 0x7B8) = 0;
            *reinterpret_cast<float*>(base + 0x7C0) = 0.0f;
            *reinterpret_cast<float*>(base + 0x7C4) = 0.0f;
            *reinterpret_cast<float*>(base + 0x7C8) = 0.0f;
            *reinterpret_cast<std::int32_t*>(base + 0x7CC) = 0;
            *reinterpret_cast<std::int32_t*>(base + 0x7D0) = 0;
        }
        else
        {
            *reinterpret_cast<std::uint64_t*>(base + 0x250) &= ~blockedButtons;
            *reinterpret_cast<std::uint64_t*>(base + 0x258) = 0;
            *reinterpret_cast<std::uint64_t*>(base + 0x260) = 0;
            *reinterpret_cast<std::uint64_t*>(base + 0x268) = 0;
            *reinterpret_cast<float*>(base + 0x270) = 0.0f;
            *reinterpret_cast<float*>(base + 0x274) = 0.0f;
            *reinterpret_cast<float*>(base + 0x278) = 0.0f;
            *reinterpret_cast<std::int32_t*>(base + 0x27C) = 0;
            *reinterpret_cast<std::int32_t*>(base + 0x280) = 0;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void Movement::setInputBlocked(void* input, bool blocked)
{
    if (!input || I::InputUsesLegacyLayout)
        return;

    __try
    {
        *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(input) + 0x228) = blocked;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

bool Movement::captureViewAngles(void* input, int slot)
{
	return captureViewAnglesSafe(input, slot);
}

void Movement::restoreViewAngles()
{
	restoreViewAnglesSafe();
}
