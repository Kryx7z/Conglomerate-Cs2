#pragma once
#include <cstdint>
#include "../../../cs2/entity/C_BaseEntity/C_BaseEntity.h"
#include "../../../cs2/entity/C_Material/C_Material.h"

#include "../../utils/math/utlstronghandle/utlstronghandle.h"

// forward declarations
class CMeshData;

enum ChamsType {
	FLAT,
	ILLUMINATE,
	GLOW,
	MAXCOUNT
};

enum ChamsEntity : std::int32_t {
	INVALID = 0,
	ENEMY,
	TEAM,
	VIEWMODEL,
	HANDS
};

enum MaterialType {
	e_visible,
	e_invisible,
	e_max_material
};

namespace chams
{
	class Materials {
	public:
		bool init();
	};

	static ChamsEntity GetTargetType(C_BaseEntity* entity, CBaseHandle sceneOwner = CBaseHandle()) noexcept;
	void __fastcall generatePrimitivesHook(void* thisptr, void* sceneObject, void* sceneView, void* primitiveBuffer);
	CStrongHandle<CMaterial2> create(const char* name, const char szVmatBuffer[]);
	std::uintptr_t __fastcall hook(void* a1, void* a2, CMeshData* arrMeshDraw, int nDataCount,
		int a5, void* a6, void* a7, void* a8);
}
