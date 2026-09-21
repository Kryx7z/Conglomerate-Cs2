#include "hooks.h"
#include "../../../external/kiero/minhook/include/MinHook.h"

#include "../../conglomerate/utils/memory/Interface/Interface.h"
#include "../utils/memory/patternscan/patternscan.h"
#include "../utils/memory/gaa/gaa.h"

#include "../features/visuals/visuals.h"
#include "../features/chams/chams.h"

#include "../../cs2/datatypes/cutlbuffer/cutlbuffer.h"
#include "../../cs2/datatypes/keyvalues/keyvalues.h"
#include "../../cs2/entity/C_Material/C_Material.h"

#include "../config/config.h"
#include "../interfaces/interfaces.h"
#include "../features/aim/aim.h"
#include "../features/movement/movement.h"
#include "../menu/menu.h"

#include <cstdio>

static bool IsExecutableAddress(const void* address)
{
	if (!address)
		return false;

	MEMORY_BASIC_INFORMATION mbi{};
	if (VirtualQuery(address, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT)
		return false;

	const DWORD protection = mbi.Protect & 0xFFu;
	return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
		protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

static bool ReadMemorySafe(std::uintptr_t address, void* output, std::size_t size)
{
	if (!address || !output || !size)
		return false;

	SIZE_T bytesRead = 0;
	return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
		output, size, &bytesRead) != FALSE && bytesRead == size;
}

static void SuppressInputSafe(void* input)
{
	__try
	{
		Movement::suppressInput(input);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

static void PrepareInputSafe(void* input, bool menuOpen)
{
	__try
	{
		Movement::setInputBlocked(input, menuOpen);
		if (menuOpen)
			Movement::suppressInput(input);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

using CreateMoveFn = void(__fastcall*)(void*, unsigned int, std::int64_t);

namespace
{
	static std::uintptr_t ResolveOverrideView(std::uintptr_t pattern)
	{
		if (!pattern)
			return 0;

		const uintptr_t leaTarget = M::getAbsoluteAddress(pattern, 0x7, 0x78);
		if (IsExecutableAddress(reinterpret_cast<void*>(leaTarget)))
			return leaTarget;

		uintptr_t indirect = 0;
		if (ReadMemorySafe(leaTarget, &indirect, sizeof(indirect)) &&
			IsExecutableAddress(reinterpret_cast<void*>(indirect)))
			return indirect;

		return 0;
	}

	static std::uintptr_t ReadExecutableTarget(std::uintptr_t address)
	{
		uintptr_t target = 0;
		if (ReadMemorySafe(address, &target, sizeof(target)) &&
			IsExecutableAddress(reinterpret_cast<void*>(target)))
			return target;

		return 0;
	}

	static std::uintptr_t ResolveHandleViewAngles()
	{
		const auto pattern = reinterpret_cast<std::uintptr_t>(M::FindPattern(
			"client", "FF FF FF FF 48 8D 05 ? ? ? ? 48 89 0D ? ? ? ?"));
		if (!pattern)
			return 0;

		return ReadExecutableTarget(M::getAbsoluteAddress(pattern, 0x7, 0x40));
	}
}

static bool CallCreateMoveSafe(CreateMoveFn original, void* input, unsigned int slot, std::int64_t active)
{
	if (!original)
		return false;

	__try
	{
		original(input, slot, active);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

static void hkFrameStageNotify_impl(void* a1, int stage)
{
	H::FrameStageNotify.GetOriginal()(a1, stage);

	// Smoke can be created during network post-update and between the render
	// callbacks. Reapply the two projectile state fields at each relevant
	// boundary so a newly spawned grenade cannot leave a particle effect behind
	// for a frame.
	if (stage == 3 || stage == 5 || stage == 6 || stage == 12)
	{
		__try
		{
			H::updateSmoke();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	// frame_render_stage - fires at render-rate on this build (confirmed empirically).
	if (stage != 12)
		return;

	__try
	{
		H::updateSky();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}

	auto lp = H::oGetLocalPlayer ? H::oGetLocalPlayer(0) : nullptr;
	if (!lp)
		return;

	__try
	{
		Esp::cache();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}

	if (!UiState::menuOpen)
	{
		__try
		{
			Aimbot();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}
}

void __fastcall H::hkFrameStageNotify(void* a1, int stage)
{
	__try
	{
		hkFrameStageNotify_impl(a1, stage);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

void* __fastcall H::hkLevelInit(void* pClientModeShared, const char* szNewMap) {
	static void* g_pPVS = (void*)M::getAbsoluteAddress(M::patternScan("engine2", "48 8D 0D ? ? ? ? 33 D2 FF 50"), 0x3);

	if (g_pPVS)
		M::vfunc<void*, 6U, void>(g_pPVS, false);

	return LevelInit.GetOriginal()(pClientModeShared, szNewMap);
}

void __fastcall H::hkCreateMove(void* input, unsigned int slot, std::int64_t active)
{
	static auto original = CreateMove.GetOriginal();
	const bool menuOpen = UiState::menuOpen;

	PrepareInputSafe(input, menuOpen);

	if (original)
	{
		if (!CallCreateMoveSafe(original, input, slot, active))
		{
			return;
		}
	}

	if (menuOpen)
	{
		PrepareInputSafe(input, true);
		SuppressInputSafe(input);
		return;
	}
}

void __fastcall H::hkHandleViewAngles(void* input, int slot)
{
	static auto original = H::HandleViewAngles.GetOriginal();
	if (!original)
		return;

	const bool restore = UiState::menuOpen && Movement::captureViewAngles(input, slot);
	original(input, slot);

	if (restore)
		Movement::restoreViewAngles();
}

void __fastcall H::hkRenderSmoke(void* a1, void* a2, int a3, int a4, void* a5, void* a6)
{
	static auto original = H::RenderSmoke.GetOriginal();

	if (!original || Config::noSmoke)
		return;

	original(a1, a2, a3, a4, a5, a6);
}
void H::Hooks::init() {
	auto scan = [](const char* module, const char* pattern) -> uintptr_t {
		return M::patternScan(module, pattern);
	};

	if (uintptr_t p = scan("client", "48 8B 81 ? ? ? ? 85 D2 78 ? 48 83 FA ? 73 ? F3 0F 10 84 90 ? ? ? ? C3 F3 0F 10 80 ? ? ? ? C3 CC CC CC CC"))
		oGetWeaponData = *reinterpret_cast<int*>(p + 0x3);
	else
		oGetWeaponData = 0;

	ogGetBaseEntity = reinterpret_cast<decltype(ogGetBaseEntity)>(scan("client", "4C 8D 49 10 81 FA FE 7F 00 00 ? ? 8B CA C1 F9 09 83 F9 3F ? ? 48 63 C1 4D"));
	oGetLocalPlayer = reinterpret_cast<decltype(oGetLocalPlayer)>(M::getAbsoluteAddress(scan("client", "E8 ? ? ? ? 48 8B F0 48 85 C0 74 ? 48 8D 15 ? ? ? ? B9"), 0x1));

	uintptr_t createMove = scan(
		"client", "48 8B C4 4C 89 40 18 48 89 48 08 55 53 41 54 41");
	if (!createMove)
		createMove = scan(
			"client", "48 8B C4 4C 89 40 ? 48 89 48 ? 55 53 41 54");
	if (createMove && IsExecutableAddress(reinterpret_cast<void*>(createMove)))
	{
		CreateMove.Add(reinterpret_cast<void*>(createMove), reinterpret_cast<void*>(&hkCreateMove));
	}
	if (const auto handleViewAngles = ResolveHandleViewAngles())
		HandleViewAngles.Add(reinterpret_cast<void*>(handleViewAngles), reinterpret_cast<void*>(&hkHandleViewAngles));

	const uintptr_t updateWallsObject = scan(
		"scenesystem", "48 8B C4 48 89 50 ? 48 89 48 ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70");
	if (IsExecutableAddress(reinterpret_cast<void*>(updateWallsObject)))
		UpdateWallsObject.Add(reinterpret_cast<void*>(updateWallsObject), &hkUpdateSceneObject);

	const uintptr_t frameStageNotify = scan(
		"client", "48 89 5C 24 ? 48 89 6C 24 ? 57 48 83 EC 40 48 8B F9 33 ED");
	if (IsExecutableAddress(reinterpret_cast<void*>(frameStageNotify)))
		FrameStageNotify.Add(reinterpret_cast<void*>(frameStageNotify), &hkFrameStageNotify);
	const uintptr_t drawArray = scan("scenesystem", "48 8B C4 53 57 41 54 48 81 EC D0 00 00 00 49 63 F9 49");
	if (IsExecutableAddress(reinterpret_cast<void*>(drawArray)))
		DrawArray.Add(reinterpret_cast<void*>(drawArray), &chams::hook);
	const uintptr_t lightSceneCall = scan(
		"scenesystem", "E8 ? ? ? ? 44 0F 28 5C 24 60");
	const uintptr_t lightSceneObject = lightSceneCall
		? M::getAbsoluteAddress(lightSceneCall, 0x1)
		: 0;
    const uintptr_t lightSceneFallback = reinterpret_cast<uintptr_t>(
        M::FindPattern("scenesystem", "48 89 54 24 ? 55 57 41 56 48 83 EC"));
	const uintptr_t updateLightObject = lightSceneObject ? lightSceneObject : lightSceneFallback;
	if (IsExecutableAddress(reinterpret_cast<void*>(updateLightObject)))
		UpdateLightObject.Add(reinterpret_cast<void*>(updateLightObject), &hkUpdateLightObject);
	const uintptr_t drawScenePattern = scan(
		"scenesystem", "48 8D 05 ? ? ? ? 48 89 07 48 8B 7C 24 48");
	const uintptr_t drawSceneObject = drawScenePattern
		? ReadExecutableTarget(M::getAbsoluteAddress(drawScenePattern, 0x3, 0x8))
		: 0;
	if (drawSceneObject)
		DrawSceneObject.Add(reinterpret_cast<void*>(drawSceneObject), &hkDrawSceneObject);
	const uintptr_t drawSkyboxArray = scan("scenesystem", "45 85 C9 0F 8E ? ? ? ? 4C 8B DC");
	if (IsExecutableAddress(reinterpret_cast<void*>(drawSkyboxArray)))
		DrawSkyboxArray.Add(reinterpret_cast<void*>(drawSkyboxArray), &hkDrawSkyboxArray);
	const uintptr_t renderSmokeCall = scan(
		"client", "5C 24 28 48 89 44 24 20 E8 ? ? ? ? 48 8B 5C 24 60");
	const uintptr_t renderSmoke = renderSmokeCall
		? M::getAbsoluteAddress(renderSmokeCall, 0x9)
		: 0;
	if (IsExecutableAddress(reinterpret_cast<void*>(renderSmoke)))
		RenderSmoke.Add(reinterpret_cast<void*>(renderSmoke), &hkRenderSmoke);
	uintptr_t renderFov = scan(
		"client", "40 53 48 83 EC ? 48 8B D9 E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8 48 83 C4");
	if (!renderFov)
	{
		renderFov = scan(
			"client", "40 53 48 83 EC 50 48 8B D9 E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8 48 83 C4 50 5B E9");
	}
	if (IsExecutableAddress(reinterpret_cast<void*>(renderFov)))
		GetRenderFov.Add(reinterpret_cast<void*>(renderFov), &hkGetRenderFov);
	// The camera view-setup path is separate from GetRenderFov on recent builds
	// and is what resets the view when AUG/SG are equipped. Resolve the current
	// view function only when the candidate is executable; this keeps older
	// builds on the existing FOV hook instead of installing an unsafe address.
	const uintptr_t viewPattern = scan("client", "A8 00 00 00 48 8D 05 ? ? ? ? 4C 89 74 24 20");
	if (viewPattern)
	{
		const uintptr_t viewTarget = ResolveOverrideView(viewPattern);
		if (viewTarget)
			OverrideView.Add(reinterpret_cast<void*>(viewTarget), reinterpret_cast<void*>(&hkOverrideView));
	}
	const uintptr_t levelInit = scan(
		"client", "40 55 56 41 56 48 8D 6C 24 ? 48 81 EC ? ? ? ? 48");
	if (IsExecutableAddress(reinterpret_cast<void*>(levelInit)))
		LevelInit.Add(reinterpret_cast<void*>(levelInit), &hkLevelInit);

	const uintptr_t renderFlashBangOverlay = scan(
		"client", "85 D2 0F 88 ? ? ? ? 48 89 4C 24 ? 55 56");
	if (IsExecutableAddress(reinterpret_cast<void*>(renderFlashBangOverlay)))
		RenderFlashBangOverlay.Add(reinterpret_cast<void*>(renderFlashBangOverlay), &hkRenderFlashbangOverlay);

	MH_EnableHook(MH_ALL_HOOKS);
}
