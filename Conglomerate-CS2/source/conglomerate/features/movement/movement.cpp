#include "movement.h"

#include <Windows.h>
#include <cstdint>

#include "../../interfaces/interfaces.h"
#include "../../utils/memory/patternscan/patternscan.h"
#include "../../utils/memory/safe_memory.h"

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
        __except (SehDiagnostics::handle("movement.capture_view_angles"))
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
		__except (SehDiagnostics::handle("movement.restore_view_angles"))
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
    const auto clearButtons = [&](std::uintptr_t address)
    {
        std::uint64_t buttons = 0;
        if (!SafeMemory::read(address, buttons))
            return;

        SafeMemory::write(address, buttons & ~blockedButtons);
    };

    const auto clearValue = [&](std::uintptr_t address, auto value)
    {
        SafeMemory::write(address, value);
    };

    if (I::InputUsesLegacyLayout)
    {
        clearButtons(base + 0x7A0);
        clearValue(base + 0x7A8, std::uint64_t{});
        clearValue(base + 0x7B0, std::uint64_t{});
        clearValue(base + 0x7B8, std::uint64_t{});
        clearValue(base + 0x7C0, 0.0f);
        clearValue(base + 0x7C4, 0.0f);
        clearValue(base + 0x7C8, 0.0f);
        clearValue(base + 0x7CC, std::int32_t{});
        clearValue(base + 0x7D0, std::int32_t{});
    }
    else
    {
        clearButtons(base + 0x250);
        clearValue(base + 0x258, std::uint64_t{});
        clearValue(base + 0x260, std::uint64_t{});
        clearValue(base + 0x268, std::uint64_t{});
        clearValue(base + 0x270, 0.0f);
        clearValue(base + 0x274, 0.0f);
        clearValue(base + 0x278, 0.0f);
        clearValue(base + 0x27C, std::int32_t{});
        clearValue(base + 0x280, std::int32_t{});
    }
}

void Movement::setInputBlocked(void* input, bool blocked)
{
    if (!input || I::InputUsesLegacyLayout)
        return;

    SafeMemory::write(reinterpret_cast<std::uintptr_t>(input) + 0x228u, blocked);
}

bool Movement::captureViewAngles(void* input, int slot)
{
	return captureViewAnglesSafe(input, slot);
}

void Movement::restoreViewAngles()
{
	restoreViewAnglesSafe();
}
