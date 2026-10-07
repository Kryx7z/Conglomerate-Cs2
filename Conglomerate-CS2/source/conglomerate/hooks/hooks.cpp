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
#include "../menu/menu_input_lock.h"
#include "../menu/menu.h"
#include "../utils/memory/seh_diagnostics.h"
#include "../utils/debug_console.h"

#include <cstdio>
#include <atomic>

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

template <typename Hook>
static void InstallHook(const char* name, const char* module, uintptr_t address, Hook& hook, void* detour)
{
	const uintptr_t moduleBase = modules.getModule(module);
	if (!moduleBase)
	{
		DebugConsole::logf("[hook] %s: module %s is unavailable", name, module);
		return;
	}
	if (!address)
	{
		DebugConsole::logf("[hook] %s: signature scan MISS in %s", name, module);
		return;
	}
	if (!IsExecutableAddress(reinterpret_cast<void*>(address)))
	{
		DebugConsole::logf("[hook] %s: match %p (RVA 0x%llX) is not executable", name,
			reinterpret_cast<void*>(address), static_cast<unsigned long long>(address - moduleBase));
		return;
	}

	const bool installed = hook.Add(reinterpret_cast<void*>(address), detour);
	DebugConsole::logf("[hook] %s: %s at %s+0x%llX; MinHook status=%d original=%p", name,
		installed ? "INSTALLED" : "ADD FAILED", module,
		static_cast<unsigned long long>(address - moduleBase),
		static_cast<int>(hook.GetLastStatus()),
		reinterpret_cast<void*>(hook.GetOriginal()));
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
	MenuInputLock::suppressInput(input);
}

static void PrepareInputSafe(void* input, bool menuOpen)
{
	MenuInputLock::setBlocked(input, menuOpen);
	if (menuOpen)
		MenuInputLock::suppressInput(input);
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
	__except (SehDiagnostics::handle("hook.create_move.original"))
	{
		return false;
	}
}

static void CallAimbotSafe(void* input, unsigned int slot)
{
	__try
	{
		Aimbot(input, static_cast<int>(slot));
	}
	__except (SehDiagnostics::handle("hook.create_move.aimbot"))
	{
	}
}

static void CallTriggerBotSafe(void* input, unsigned int slot, bool allowed)
{
	__try
	{
		TriggerBot(input, static_cast<int>(slot), allowed);
	}
	__except (SehDiagnostics::handle("hook.create_move.triggerbot"))
	{
		ReleaseTriggerBot();
	}
}

static void hkFrameStageNotify_impl(void* a1, int stage)
{
	H::FrameStageNotify.GetOriginal()(a1, stage);
	static std::atomic<std::uint32_t> observedStages{};
	if (stage >= 0 && stage < 32)
	{
		const std::uint32_t mask = 1u << static_cast<unsigned int>(stage);
		if ((observedStages.fetch_or(mask, std::memory_order_relaxed) & mask) == 0u)
			DebugConsole::logf("[runtime] FrameStageNotify first observed stage=%d", stage);
	}

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
		__except (SehDiagnostics::handle("hook.frame_stage.smoke"))
		{
		}
	}

	// frame_render_stage: fires at render-rate on this build (confirmed empirically).
	if (stage != 12)
		return;
	DebugConsole::once("frame_stage_12", "[runtime] FrameStageNotify reached stage 12");

	auto lp = H::oGetLocalPlayer ? H::oGetLocalPlayer(0) : nullptr;
	if (!lp)
	{
		DebugConsole::rateLimited("get_local_pawn.null", "[runtime] GetLocalPawn returned null during stage 12");
		return;
	}
	DebugConsole::once("get_local_pawn.nonnull", "[runtime] GetLocalPawn returned a pawn during stage 12");

	__try
	{
		Esp::cache();
		DebugConsole::once("esp.cache.called", "[runtime] ESP cache ran; cached player count=%llu",
			static_cast<unsigned long long>(cached_players.size()));
	}
	__except (SehDiagnostics::handle("hook.frame_stage.esp"))
	{
	}

}

void __fastcall H::hkFrameStageNotify(void* a1, int stage)
{
	__try
	{
		hkFrameStageNotify_impl(a1, stage);
	}
	__except (SehDiagnostics::handle("hook.frame_stage"))
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
	DebugConsole::once("createmove.hook.first", "[runtime] CreateMove detour reached; input=%p slot=%u", input, slot);
	static auto original = CreateMove.GetOriginal();
	const bool menuOpen = UiState::menuOpen;

	PrepareInputSafe(input, menuOpen);
	if (!menuOpen && Config::bunnyHop && oGetLocalPlayer)
		Movement::applyBunnyHopInput(oGetLocalPlayer(0));
	// Trigger input must be written before the original CreateMove builds this tick's command.
	// Writing the button after the original callback loses the press before it reaches the command.
	CallTriggerBotSafe(input, slot, !menuOpen);

	if (original)
	{
		if (!CallCreateMoveSafe(original, input, slot, active))
		{
			ReleaseTriggerBot();
			return;
		}
	}

	if (menuOpen)
	{
		PrepareInputSafe(input, true);
		SuppressInputSafe(input);
		return;
	}
	if (Config::aimbot)
		DebugConsole::once("aim.dispatch", "[runtime] aim dispatch reached from CreateMove; enabled=1");
	CallAimbotSafe(input, slot);
}

void __fastcall H::hkHandleViewAngles(void* input, int slot)
{
	static auto original = H::HandleViewAngles.GetOriginal();
	if (!original)
		return;

	const bool restore = UiState::menuOpen && MenuInputLock::captureViewAngles(input, slot);
	original(input, slot);

	if (restore)
		MenuInputLock::restoreViewAngles();
}

void __fastcall H::hkRenderSmoke(void* a1, void* a2, int a3, int a4, void* a5, void* a6)
{
	DebugConsole::once("smoke.hook.first", "[runtime] RenderSmoke detour reached; noSmoke=%d", Config::noSmoke ? 1 : 0);
	static auto original = H::RenderSmoke.GetOriginal();

	if (!original)
	{
		DebugConsole::rateLimited("smoke.original.missing", "[runtime] RenderSmoke original function is null");
		return;
	}
	if (Config::noSmoke)
		return;

	original(a1, a2, a3, a4, a5, a6);
}
void H::Hooks::init() {
	auto scan = [](const char* module, const char* pattern) -> uintptr_t {
		return M::patternScan(module, pattern);
	};

	if (uintptr_t p = scan("client", "48 8B 81 ? ? ? ? 85 D2 78 ? 48 83 FA ? 73 ? F3 0F 10 84 90 ? ? ? ? C3 F3 0F 10 80 ? ? ? ? C3 CC CC CC CC"))
	{
		oGetWeaponData = *reinterpret_cast<int*>(p + 0x3);
		DebugConsole::logf("[scan] GetWeaponData: FOUND at %p (client RVA 0x%llX)",
			reinterpret_cast<void*>(p), static_cast<unsigned long long>(p - modules.getModule("client")));
	}
	else
	{
		oGetWeaponData = 0;
		DebugConsole::logf("[scan] GetWeaponData: MISS");
	}

	ogGetBaseEntity = reinterpret_cast<decltype(ogGetBaseEntity)>(scan("client", "4C 8D 49 10 81 FA FE 7F 00 00 ? ? 8B CA C1 F9 09 83 F9 3F ? ? 48 63 C1 4D"));
	DebugConsole::logf("[scan] GetBaseEntity: %s at %p", ogGetBaseEntity ? "FOUND" : "MISS",
		reinterpret_cast<void*>(ogGetBaseEntity));
	const uintptr_t getLocalPawn = scan(
		"client", "48 83 EC ? 83 F9 ? 75 ? 48 8B 0D ? ? ? ? 48 8D 54 24 ? ? ? ? FF 90 ? ? ? ? ? ? 48 63 C1 4C 8D 05");
	oGetLocalPlayer = IsExecutableAddress(reinterpret_cast<void*>(getLocalPawn))
		? reinterpret_cast<decltype(oGetLocalPlayer)>(getLocalPawn)
		: nullptr;
	DebugConsole::logf("[scan] GetLocalPawn: %s at %p (client RVA 0x%llX)",
		oGetLocalPlayer ? "FOUND" : "MISS/INVALID", reinterpret_cast<void*>(getLocalPawn),
		static_cast<unsigned long long>(getLocalPawn && modules.getModule("client")
			? getLocalPawn - modules.getModule("client") : 0));

	uintptr_t createMove = scan(
		"client", "48 8B C4 4C 89 40 ? 48 89 48 ? 55 53 57");
	if (!createMove)
		createMove = scan(
			"client", "48 8B C4 4C 89 40 ? 48 89 48 ? 55 53 41 54");
	InstallHook("CreateMove", "client", createMove, CreateMove, reinterpret_cast<void*>(&hkCreateMove));
	const auto handleViewAngles = ResolveHandleViewAngles();
	InstallHook("HandleViewAngles", "client", handleViewAngles, HandleViewAngles,
		reinterpret_cast<void*>(&hkHandleViewAngles));

	// UpdateWallsObject is DrawAggregateSceneObjectArray in scenesystem.dll.
	const uintptr_t updateWallsObject = scan(
		"scenesystem", "48 8B C4 48 89 50 ? 48 89 48 ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70");
	InstallHook("UpdateWallsObject/DrawAggregateSceneObjectArray", "scenesystem", updateWallsObject,
		UpdateWallsObject, reinterpret_cast<void*>(&hkUpdateSceneObject));
	const uintptr_t setShaderParamPattern = scan(
		"client", "BA 7E 99 0D EB 48 8B CB 66 0F 7F 45 F0 E8 ? ? ? ? F3 0F 10 4D D0");
	const uintptr_t setShaderParam = setShaderParamPattern
		? M::getAbsoluteAddress(setShaderParamPattern, 0xE)
		: 0;
	InstallHook("SetShaderParam/NightMode", "client", setShaderParam, SetShaderParam,
		reinterpret_cast<void*>(&hkSetShaderParam));
	const uintptr_t setPostprocessVecCall = scan(
		"engine2", "E8 ? ? ? ? 44 0F 28 94 24");
	const uintptr_t setPostprocessVec = setPostprocessVecCall
		? M::getAbsoluteAddress(setPostprocessVecCall, 0x1)
		: 0;
	InstallHook("SetPostprocessVec/NightMode", "engine2", setPostprocessVec, SetPostprocessVec,
		reinterpret_cast<void*>(&hkSetPostprocessVec));
	const uintptr_t clientBase = modules.getModule("client");
	const uintptr_t frameStageNotify = clientBase ? clientBase + 0xB6D440 : 0;
	InstallHook("FrameStageNotify", "client", frameStageNotify, FrameStageNotify,
		reinterpret_cast<void*>(&hkFrameStageNotify));
	// DrawArray is DrawAggeregateObject (SDK spelling) in scenesystem.dll.
	const uintptr_t drawArray = scan("scenesystem", "48 8B C4 4C 89 40 ? 48 89 50 ? 55 53 41 57");
	InstallHook("DrawArray/DrawAggeregateObject", "scenesystem", drawArray, DrawArray,
		reinterpret_cast<void*>(&chams::hook));
	// Chams need the scene object and primitive buffer before the renderer batches
	// meshes. This is the GeneratePrimitives path used by the supplied reference.
	const uintptr_t generatePrimitives = scan(
		"scenesystem", "48 8B C4 48 89 58 20 4C 89 40 18 48 89 50 10 48 89 48 08");
	InstallHook("GeneratePrimitives/chams", "scenesystem", generatePrimitives, GeneratePrimitives,
		reinterpret_cast<void*>(&chams::generatePrimitivesHook));
	const uintptr_t lightSceneCall = scan(
		"scenesystem", "E8 ? ? ? ? 44 0F 28 5C 24 60");
	const uintptr_t lightSceneObject = lightSceneCall
		? M::getAbsoluteAddress(lightSceneCall, 0x1)
		: 0;
    const uintptr_t lightSceneFallback = reinterpret_cast<uintptr_t>(
        M::FindPattern("scenesystem", "48 89 54 24 ? 55 57 41 56 48 83 EC"));
	const uintptr_t updateLightObject = lightSceneObject ? lightSceneObject : lightSceneFallback;
	InstallHook("UpdateLightObject", "scenesystem", updateLightObject, UpdateLightObject,
		reinterpret_cast<void*>(&hkUpdateLightObject));
	const uintptr_t drawScenePattern = scan(
		"scenesystem", "48 8D 05 ? ? ? ? 48 89 07 48 8B 7C 24 48");
	const uintptr_t drawSceneObject = drawScenePattern
		? ReadExecutableTarget(M::getAbsoluteAddress(drawScenePattern, 0x3, 0x8))
		: 0;
	if (!drawSceneObject)
		InstallHook("DrawSceneObject", "scenesystem", 0, DrawSceneObject,
			reinterpret_cast<void*>(&hkDrawSceneObject));
	else if (drawSceneObject == drawArray)
		DebugConsole::logf("[hook] DrawSceneObject shares DrawArray target; primitive night coloring merged into DrawArray");
	else
		InstallHook("DrawSceneObject", "scenesystem", drawSceneObject, DrawSceneObject,
			reinterpret_cast<void*>(&hkDrawSceneObject));
	const uintptr_t drawSkyboxArray = scan("scenesystem", "45 85 C9 0F 8E ? ? ? ? 4C 8B DC");
	InstallHook("DrawSkyboxArray", "scenesystem", drawSkyboxArray, DrawSkyboxArray,
		reinterpret_cast<void*>(&hkDrawSkyboxArray));
	const uintptr_t renderSmokeCall = scan(
		"client", "5C 24 28 48 89 44 24 20 E8 ? ? ? ? 48 8B 5C 24 60");
	const uintptr_t renderSmoke = renderSmokeCall
		? M::getAbsoluteAddress(renderSmokeCall, 0x9)
		: 0;
	InstallHook("RenderSmoke", "client", renderSmoke, RenderSmoke,
		reinterpret_cast<void*>(&hkRenderSmoke));
	uintptr_t renderFov = scan(
		"client", "40 53 48 83 EC ? 48 8B D9 E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8 48 83 C4");
	if (!renderFov)
	{
		renderFov = scan(
			"client", "40 53 48 83 EC 50 48 8B D9 E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8 48 83 C4 50 5B E9");
	}
	InstallHook("GetRenderFov", "client", renderFov, GetRenderFov,
		reinterpret_cast<void*>(&hkGetRenderFov));
	// The camera view-setup path is separate from GetRenderFov on recent builds
	// and is what resets the view when AUG/SG are equipped. Resolve the current
	// view function only when the candidate is executable; this keeps older
	// builds on the existing FOV hook instead of installing an unsafe address.
	const uintptr_t viewPattern = scan("client", "A8 00 00 00 48 8D 05 ? ? ? ? 4C 89 74 24 20");
	if (viewPattern)
	{
		const uintptr_t viewTarget = ResolveOverrideView(viewPattern);
		InstallHook("OverrideView", "client", viewTarget, OverrideView,
			reinterpret_cast<void*>(&hkOverrideView));
	}
	else
	{
		DebugConsole::logf("[hook] OverrideView: signature scan MISS in client");
	}
	const uintptr_t levelInit = scan(
		"client", "48 89 74 24 ? 57 48 83 EC ? 48 8B 0D ? ? ? ? 48 8B FA");
	InstallHook("LevelInit", "client", levelInit, LevelInit,
		reinterpret_cast<void*>(&hkLevelInit));

	const uintptr_t renderFlashBangOverlay = scan(
		"client", "85 D2 0F 88 ? ? ? ? 48 89 4C 24 ? 55 56");
	InstallHook("RenderFlashBangOverlay", "client", renderFlashBangOverlay, RenderFlashBangOverlay,
		reinterpret_cast<void*>(&hkRenderFlashbangOverlay));

	const MH_STATUS enableStatus = MH_EnableHook(MH_ALL_HOOKS);
	DebugConsole::logf("[hook] MH_EnableHook(MH_ALL_HOOKS): status=%d", static_cast<int>(enableStatus));
}
