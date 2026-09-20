#pragma once
#include "../../../conglomerate/utils/memory/memorycommon.h"
#include "../../../conglomerate/utils/math/vector/vector.h"
#include "../../../conglomerate/utils/schema/schema.h"
#include "../C_CSWeaponBase/C_CSWeaponBase.h"
#include "../C_BaseEntity/C_BaseEntity.h"

#include <cstdint>

class CPlayer_ObserverServices
{
public:
	 schema(CBaseHandle, m_hObserverTarget, "CPlayer_ObserverServices->m_hObserverTarget");
};

class C_CSPlayerPawn : public C_BaseEntity {
public:
	SCHEMA_ADD_OFFSET(Vector_t, m_vOldOrigin, 0x13B8);
	SCHEMA_ADD_OFFSET(Vector_t, m_vecViewOffset, 0xE78);
	SCHEMA_ADD_OFFSET(CCSPlayer_WeaponServices*, m_pWeaponServices, 0x1208);
	schema(bool, m_bIsScoped, "C_CSPlayerPawn->m_bIsScoped");
	schema(float, m_flFlashDuration, "C_CSPlayerPawnBase->m_flFlashDuration");
	schema(void*, m_pGameSceneNode, "C_BaseEntity->m_pGameSceneNode");
	schema(CPlayer_ObserverServices*, m_pObserverServices, "C_BasePlayerPawn->m_pObserverServices");
	C_CSPlayerPawn(uintptr_t address);

	C_CSWeaponBase* GetActiveWeapon()const;
	CCSPlayer_WeaponServices* GetWeaponServices()const;
	Vector_t getPosition() const;
	Vector_t getEyePosition() const;

	uintptr_t getAddress() const;
	int getHealth() const;
	uint8_t getTeam() const;
	Vector_t getViewOffset() const;
private:
	uintptr_t address;
};
