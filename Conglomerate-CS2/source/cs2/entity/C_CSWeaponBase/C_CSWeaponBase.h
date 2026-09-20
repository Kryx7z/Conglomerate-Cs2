#pragma once
#include <cstdint>
#include "..\C_EntityInstance\C_EntityInstance.h"
#include "../../../conglomerate/utils/memory/memorycommon.h"
#include "../../../conglomerate/utils/math/vector/vector.h"
#include "../../../conglomerate/utils/math/utlvector/utlvector.h"
#include "..\..\..\..\source\conglomerate\utils\schema\schema.h"
#include "..\..\..\..\source\conglomerate\utils\memory\vfunc\vfunc.h"
#include "..\handle.h"

class CCSPlayer_WeaponServices
{
public:
	schema(bool, m_bAllowSwitchToNoWeapon, "CPlayer_WeaponServices->m_bAllowSwitchToNoWeapon");
	schema(C_NetworkUtlVectorBase<CBaseHandle>, m_hMyWeapons, "CPlayer_WeaponServices->m_hMyWeapons");
	schema(CBaseHandle, m_hActiveWeapon, "CPlayer_WeaponServices->m_hActiveWeapon");
	schema(CBaseHandle, m_hLastWeapon, "CPlayer_WeaponServices->m_hLastWeapon");
	schema(int, m_iAmmo, "CPlayer_WeaponServices->m_iAmmo");
	schema(float, m_flNextAttack, "CCSPlayer_WeaponServices->m_flNextAttack");
	schema(bool, m_bIsLookingAtWeapon, "CCSPlayer_WeaponServices->m_bIsLookingAtWeapon");
	schema(bool, m_bIsHoldingLookAtWeapon, "CCSPlayer_WeaponServices->m_bIsHoldingLookAtWeapon");
};

class CCSWeaponBaseVData
{
public:
	SCHEMA_ADD_OFFSET(const char*, m_szName, 0x720); // CCSWeaponBaseVData->m_szName
};

class C_CSWeaponBase 
{
public:
	schema(int, m_zoomLevel, "CWeaponCSBase->m_zoomLevel");

	// @todo: add few schemas here
	CCSWeaponBaseVData* Data();
};
