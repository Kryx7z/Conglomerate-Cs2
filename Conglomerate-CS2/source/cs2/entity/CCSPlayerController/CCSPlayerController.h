#pragma once
#include <cstdint>
#pragma once
#include "../../../conglomerate/utils/memory/memorycommon.h"
#include "../../../conglomerate/utils/math/vector/vector.h"
#include "../../../conglomerate/utils/schema/schema.h"
#include "../C_CSWeaponBase/C_CSWeaponBase.h"
#include <cstdint>
class CCSPlayerController {
public:
	CCSPlayerController(uintptr_t address);
	const char* getName() const;
	uintptr_t getAddress() const;

	SCHEMA_ADD_OFFSET(bool, IsLocalPlayer, 0x788);
	SCHEMA_ADD_OFFSET(CBaseHandle, m_hPawn, 0x6BC);
	schema(CBaseHandle, m_hObserverPawn, "CCSPlayerController->m_hObserverPawn");
	schema(bool, m_bPawnIsAlive, "CCSPlayerController->m_bPawnIsAlive");
	SCHEMA_ADD_OFFSET(const char*, m_sSanitizedPlayerName, 0x868);

private:
	uintptr_t address;
};
