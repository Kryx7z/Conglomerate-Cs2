#pragma once
#include <cstdint>
#include "../C_EntityInstance/C_EntityInstance.h"
#include "../../../conglomerate/utils/memory/memorycommon.h"
#include "../../../conglomerate/utils/math/vector/vector.h"
#include "../../../../source/conglomerate/utils/schema/schema.h"
#include "../../../../source/conglomerate/utils/memory/vfunc/vfunc.h"
#include "../handle.h"

class C_BaseEntity : public CEntityInstance
{
public:
	schema(int, m_iMaxHealth, "C_BaseEntity->m_iMaxHealth");
	SCHEMA_ADD_OFFSET(int, m_iHealth, 0x34C);
	SCHEMA_ADD_OFFSET(int, m_iTeamNum, 0x3E7);

	bool IsBasePlayer()
	{
		SchemaClassInfoData_t* pClassInfo;
		dump_class_info(&pClassInfo);
		if (pClassInfo == nullptr)
			return false;

		return hash_32_fnv1a_const(pClassInfo->szName) == hash_32_fnv1a_const("C_CSPlayerPawn");
	}

	bool IsViewmodelAttachment()
	{
		SchemaClassInfoData_t* pClassInfo;
		dump_class_info(&pClassInfo);
		if (pClassInfo == nullptr)
			return false;

		const uint32_t hash = hash_32_fnv1a_const(pClassInfo->szName);
		return hash == hash_32_fnv1a_const("C_ViewmodelAttachmentModel") ||
			hash == hash_32_fnv1a_const("C_CS2HudModelArms");
	}

	bool IsViewmodel()
	{
		SchemaClassInfoData_t* pClassInfo;
		dump_class_info(&pClassInfo);
		if (pClassInfo == nullptr)
			return false;

		const uint32_t hash = hash_32_fnv1a_const(pClassInfo->szName);
		return hash == hash_32_fnv1a_const("C_CSGOViewModel") ||
			hash == hash_32_fnv1a_const("C_CS2HudModelWeapon");
	}

	bool IsPlayerController()
	{
		SchemaClassInfoData_t* _class = nullptr;
		dump_class_info(&_class);
		if (!_class)
			return false;

		const uint32_t hash = hash_32_fnv1a_const(_class->szName);

		return (hash == hash_32_fnv1a_const("CCSPlayerController"));
	}
};
