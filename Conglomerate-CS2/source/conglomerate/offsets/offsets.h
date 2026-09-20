#pragma once
#include <cstddef>

namespace Offset {
	constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x23CCC08;

	namespace C_BasePlayerPawn {
		constexpr std::ptrdiff_t m_vOldOrigin = 0x13B8;
		constexpr std::ptrdiff_t m_iHealth = 0x34C;
		constexpr std::ptrdiff_t m_iTeamNum = 0x3E7;
		constexpr std::ptrdiff_t m_vecViewOffset = 0xE78;
	}
}
