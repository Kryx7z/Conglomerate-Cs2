#include "movement.h"

#include <Windows.h>
#include <cstdint>

#include "../../utils/memory/safe_memory.h"
#include "../../utils/memory/seh_diagnostics.h"
#include "../../utils/schema/schema.h"
#include "../../offsets/buttons.hpp"
#include "../../utils/debug_console.h"
#include "../../config/config.h"

extern HWND window;

namespace
{
	constexpr std::uintptr_t jumpButtonOffset = cs2_dumper::buttons::jump;
	constexpr std::int32_t jumpPressed = 65537;
	constexpr std::int32_t jumpReleased = 256;
	bool jumpInputPressed = false;
	bool jumpStateInitialized = false;
	std::uintptr_t trackedPawn = 0;

	bool writeJumpState(std::int32_t state)
	{
		const auto clientBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("client.dll"));
		return clientBase && SafeMemory::write(clientBase + jumpButtonOffset, state);
	}

	void releaseJumpInput()
	{
	if (jumpStateInitialized && jumpInputPressed && writeJumpState(jumpReleased))
		jumpInputPressed = false;
	}

	bool resolveBunnyHopOffsets(std::uint32_t& flagsOffset, std::uint32_t& moveTypeOffset)
	{
		__try
		{
			flagsOffset = SchemaFinder::Get("C_BaseEntity->m_fFlags");
			moveTypeOffset = SchemaFinder::Get("C_BaseEntity->m_nActualMoveType");
			return flagsOffset != 0 && moveTypeOffset != 0;
		}
		__except (SehDiagnostics::handle("movement.bunny_hop.schema_offsets"))
		{
			flagsOffset = 0;
			moveTypeOffset = 0;
			return false;
		}
	}
}

void Movement::applyBunnyHopInput(void* localPawn)
{
	if (!Config::bunnyHop || !localPawn || !window || GetForegroundWindow() != window)
	{
		releaseJumpInput();
		return;
	}

	const auto pawn = reinterpret_cast<std::uintptr_t>(localPawn);
	if (trackedPawn != pawn)
	{
		releaseJumpInput();
		trackedPawn = pawn;
		jumpInputPressed = false;
		jumpStateInitialized = false;
		DebugConsole::logf("[movement] Bunny Hop pawn changed; input state reset (pawn=%p)", localPawn);
	}

	std::uint32_t flagsOffset = 0;
	std::uint32_t moveTypeOffset = 0;
	if (!resolveBunnyHopOffsets(flagsOffset, moveTypeOffset))
	{
		releaseJumpInput();
		return;
	}

	std::uint32_t flags = 0;
	std::uint8_t moveType = 0;
	if (!SafeMemory::read(pawn + flagsOffset, flags) ||
		!SafeMemory::read(pawn + moveTypeOffset, moveType))
	{
		releaseJumpInput();
		return;
	}

	if (moveType == 9 || moveType == 7)
	{
		releaseJumpInput();
		return;
	}

	const bool spaceHeld = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
	const bool onGround = (flags & 1u) != 0;
	const bool shouldPressJump = spaceHeld && onGround;
	if (jumpStateInitialized && shouldPressJump == jumpInputPressed)
		return;
	if (!writeJumpState(shouldPressJump ? jumpPressed : jumpReleased))
	{
		DebugConsole::rateLimited("movement.bhop.write_failed",
			"[movement] Bunny Hop could not write jump state (held=%d grounded=%d flags=0x%X moveType=%u)",
			spaceHeld ? 1 : 0, onGround ? 1 : 0, flags, static_cast<unsigned int>(moveType));
		return;
	}
	jumpInputPressed = shouldPressJump;
	jumpStateInitialized = true;
	DebugConsole::logf("[movement] Bunny Hop jump %s (held=%d grounded=%d flags=0x%X moveType=%u)",
		shouldPressJump ? "pressed" : "released", spaceHeld ? 1 : 0,
		onGround ? 1 : 0, flags, static_cast<unsigned int>(moveType));
}
