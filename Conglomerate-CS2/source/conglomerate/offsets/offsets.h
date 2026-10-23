#pragma once
#include <cstddef>

#include "offsets.hpp"
#include "buttons.hpp"

// Single place that records which game build the RVAs in offsets.hpp /
// buttons.hpp were dumped from. Update both files (cs2-dumper output) and this
// number together when the game updates. The runtime logs a warning when the
// running game build differs.
namespace Offset {
	constexpr int kExpectedGameBuild = 14192;

	constexpr std::ptrdiff_t dwLocalPlayerPawn = cs2_dumper::offsets::client_dll::dwLocalPlayerPawn;
	constexpr std::ptrdiff_t dwViewMatrix = cs2_dumper::offsets::client_dll::dwViewMatrix;
	constexpr std::ptrdiff_t dwBuildNumber = cs2_dumper::offsets::engine2_dll::dwBuildNumber; // engine2.dll

	namespace C_BasePlayerPawn {
		constexpr std::ptrdiff_t m_vOldOrigin = 0x14A4;
		constexpr std::ptrdiff_t m_iHealth = 0x34C;
		constexpr std::ptrdiff_t m_iTeamNum = 0x3E7;
		constexpr std::ptrdiff_t m_vecViewOffset = 0xF60;
	}
}
