#pragma once
#include "IEngineClient/IEngineClient.h"
#include "CGameEntitySystem/CGameEntitySystem.h"
#include "ISceneSystem/iscenesystem.h"
#include "..\..\cs2\entity\C_CSPlayerPawn\C_CSPlayerPawn.h"
#include "..\..\cs2\datatypes\cutlbuffer\cutlbuffer.h"
#include "..\..\cs2\datatypes\keyvalues\keyvalues.h"
#include "..\..\cs2\entity\C_Material\C_Material.h"

namespace I
{
	inline void(__fastcall* EnsureCapacityBuffer)(CUtlBuffer*, int) = nullptr;
	inline CUtlBuffer* (__fastcall* ConstructUtlBuffer)(CUtlBuffer*, int, int, int) = nullptr;
	inline void(__fastcall* PutUtlString)(CUtlBuffer*, const char*) = nullptr;
	// The fifth parameter is consumed as a 64-bit resource-binding pointer by
	// materialsystem2.  Declaring it as unsigned int leaves the upper half of
	// the stack slot undefined and causes CreateMaterial to dereference garbage.
	inline std::int64_t(__fastcall* CreateMaterial)(void*, void*, const char*, void*, void*, unsigned int) = nullptr;

	// Current builds export the six-argument KV3 loader from client.dll. The
	// older internal loader is kept below as a compatibility fallback.
	inline bool(__fastcall* LoadKV3TextExport)(CKeyValues3*, void*, const char*, const KV3ID_t*, const KV3ID_t*, unsigned int) = nullptr;
	inline bool(__cdecl* LoadKV3Export)(CKeyValues3*, void*, CUtlBuffer*, const KV3ID_t&, const char*, unsigned int) = nullptr;
	inline bool(__fastcall* LoadKeyValues)(CKeyValues3*, void*, const char*, const KV3ID_t*, const char*) = nullptr;

	// Logging functions

	inline IEngineClient* EngineClient = nullptr;
	inline IGameResourceService* GameEntity = nullptr;
	inline void* Input = nullptr;
	inline void* InputSystem = nullptr;
	inline bool InputUsesLegacyLayout = false;
	inline ISceneSystem* SceneSystem = nullptr;
	inline void* MaterialSystem2 = nullptr;
	class Interfaces {
	public:
		bool init();
	};
}
