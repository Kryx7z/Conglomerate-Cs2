#pragma once
#include "includeHooks.h"
#include <cstddef>
#include <cstdint>
#include <emmintrin.h>
#include <xmmintrin.h>
#include "../../cs2/entity/C_AggregateSceneObject/C_AggregateSceneObject.h"
#include "../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../cs2/datatypes/cutlbuffer/cutlbuffer.h"
#include "../../cs2/datatypes/keyvalues/keyvalues.h"
#include "../../cs2/entity/C_Material/C_Material.h"

// Forward declaration
class CMeshData;
class CEntityIdentity;

namespace H {
	void __fastcall hkUpdateSceneObject(void* a1, void* a2, c_aggregate_object_array* a3);
	std::uintptr_t __fastcall hkSetShaderParam(__m128i* map, std::uint32_t hash, __m128i* value);
	std::uintptr_t __fastcall hkSetPostprocessVec(__m128i* map, std::uint32_t hash, __m128i* value);
	std::uintptr_t __fastcall hkUpdateLightObject(void* a1, void* a2, void* a3);
	std::uintptr_t __fastcall hkDrawSceneObject(void* a1, void* a2, void* batch,
		int batchCount, int a5, void* a6, void* a7, void* a8);
	void updateSky();
	void applyNightPrimitiveColor(void* batch, int batchCount);
	void updateSmoke();
	void __fastcall hkFrameStageNotify(void* a1, int stage);
	void __fastcall hkCreateMove(void* input, unsigned int slot, std::int64_t active);
	void __fastcall hkHandleViewAngles(void* input, int slot);
	void* __fastcall hkLevelInit(void* pClientModeShared, const char* szNewMap);
	std::uintptr_t __fastcall hkChamsObject(void* a1, void* a2, CMeshData* arrMeshDraw, int nDataCount,
		int a5, void* a6, void* a7, void* a8);
	void __fastcall hkRenderFlashbangOverlay(void* a1, void* a2, void* a3, void* a4, void* a5);
	void __fastcall hkRenderSmoke(void* a1, void* a2, int a3, int a4, void* a5, void* a6);
	void __fastcall hkDrawSkyboxArray(void* a1, void* a2, void* meshArray, int meshCount,
		void* a5, void* a6, void* a7, void* a8);
	void __fastcall hkOverrideView(void* self, void* viewSetup);
	inline float g_flActiveFov;
	float hkGetRenderFov(void* rcx);

	inline CInlineHookObj<decltype(&hkChamsObject)> DrawArray = { };
	inline CInlineHookObj<void(__fastcall*)(void*, void*, void*, void*)> GeneratePrimitives = { };
	inline CInlineHookObj<decltype(&hkFrameStageNotify)> FrameStageNotify = { };
	inline CInlineHookObj<decltype(&hkCreateMove)> CreateMove = { };
	inline CInlineHookObj<decltype(&hkHandleViewAngles)> HandleViewAngles = { };
	inline CInlineHookObj<decltype(&hkUpdateSceneObject)> UpdateWallsObject = { };
	inline CInlineHookObj<decltype(&hkSetShaderParam)> SetShaderParam = { };
	inline CInlineHookObj<decltype(&hkSetPostprocessVec)> SetPostprocessVec = { };
	inline CInlineHookObj<decltype(&hkUpdateLightObject)> UpdateLightObject = { };
	inline CInlineHookObj<decltype(&hkDrawSceneObject)> DrawSceneObject = { };
	inline CInlineHookObj<decltype(&hkGetRenderFov)> GetRenderFov = { };
	inline CInlineHookObj<decltype(&hkLevelInit)> LevelInit = { };
	inline CInlineHookObj<decltype(&hkRenderFlashbangOverlay)> RenderFlashBangOverlay = { };
	inline CInlineHookObj<decltype(&hkRenderSmoke)> RenderSmoke = { };
	inline CInlineHookObj<decltype(&hkDrawSkyboxArray)> DrawSkyboxArray = { };
	inline CInlineHookObj<decltype(&hkOverrideView)> OverrideView = { };

	// inline hooks
	inline int  oGetWeaponData;
	inline void* (__fastcall* ogGetBaseEntity)(void*, int);
	inline  C_CSPlayerPawn* (__fastcall* oGetLocalPlayer)(int);

	class Hooks {
	public:
		void init();
	};
}
