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
#include "../offsets/offsets.h"
#include "../utils/memory/safe_memory.h"
#include "../interfaces/interfaces.h"
#include "../features/aim/aim.h"
#include "../features/movement/movement.h"
#include "../features/skins/skins.h"
#include "../menu/menu_input_lock.h"
#include "../menu/menu.h"
#include "../utils/memory/seh_diagnostics.h"
#include "../utils/debug_console.h"
#include "../features/world/weather_rain_asset.h"

#include <cstdio>
#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

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

static bool WriteMemorySafe(std::uintptr_t address, const void* input, std::size_t size)
{
	if (!address || !input || !size)
		return false;

	SIZE_T bytesWritten = 0;
	return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
		input, size, &bytesWritten) != FALSE && bytesWritten == size;
}

static std::uintptr_t AllocateAsyncFileBuffer(std::uintptr_t asyncFileSystem,
	std::size_t size, const char* path)
{
	std::uintptr_t vtable = 0;
	std::uintptr_t allocate = 0;
	if (!SafeMemory::read(asyncFileSystem, vtable) || !vtable ||
		!SafeMemory::read(vtable + 22 * sizeof(std::uintptr_t), allocate) || !allocate)
		return 0;

	using AllocateFn = std::uintptr_t(__fastcall*)(std::uintptr_t, std::size_t, const char*);
	if (!IsExecutableAddress(reinterpret_cast<void*>(allocate)))
		return 0;
	return reinterpret_cast<AllocateFn>(allocate)(asyncFileSystem, size, path);
}

static void CloseEmbeddedFileRequest(std::uintptr_t request)
{
	static const auto closePattern = M::patternScan("filesystem_stdio",
		"48 8D 8F 20 FF FF FF 8B D5 E8 ? ? ? ? FF D3");
	static const auto closeFile = closePattern
		? M::getAbsoluteAddress(closePattern, 0xA)
		: 0;
	if (!closeFile || !IsExecutableAddress(reinterpret_cast<void*>(closeFile)))
		return;

	using CloseFn = void(__fastcall*)(std::uintptr_t, int);
	reinterpret_cast<CloseFn>(closeFile)(request - 224, 0);
}

std::uintptr_t __fastcall H::hkFileServiceRead(std::uintptr_t request)
{
	const auto original = H::FileServiceRead.GetOriginal();
	if (!original)
		return 0;

	std::uint32_t flagsAndLength = 0;
	char path[512]{};
	if (request >= 224 && SafeMemory::read(request - 212, flagsAndLength))
	{
		const auto length = static_cast<std::size_t>(flagsAndLength & 0x3FFFFFFFu);
		if (length > 0 && length < sizeof(path))
		{
			if (flagsAndLength & 0x40000000u)
			{
				ReadMemorySafe(request - 208, path, length);
			}
			else
			{
				std::uintptr_t pathPointer = 0;
				if (SafeMemory::read(request - 208, pathPointer) && pathPointer)
					ReadMemorySafe(pathPointer, path, length);
			}
		}
	}

	if (std::strstr(path, "particles/embedded/rain.vpcf"))
	{
		std::uintptr_t asyncFileSystem = 0;
		if (SafeMemory::read(request + 24, asyncFileSystem) && asyncFileSystem)
		{
			const auto buffer = AllocateAsyncFileBuffer(asyncFileSystem,
				kWeatherRainParticleSize, path);
			if (buffer && WriteMemorySafe(buffer, kWeatherRainParticle,
				kWeatherRainParticleSize) &&
				SafeMemory::write(request + 56, buffer) &&
				SafeMemory::write(request + 64, kWeatherRainParticleSize) &&
				SafeMemory::write(request + 72, kWeatherRainParticleSize))
			{
				CloseEmbeddedFileRequest(request);
				DebugConsole::once("weather.rain.asset_loaded",
					"[weather] embedded rain particle resource supplied: bytes=%zu", kWeatherRainParticleSize);
				return 0;
			}
		}

		DebugConsole::rateLimited("weather.rain.asset_failed",
			"[weather] embedded rain resource request could not be fulfilled");
	}

	return original(request);
}

struct ExecutablePatternScanResult
{
	std::uintptr_t address = 0;
	std::size_t matchCount = 0;
	bool complete = false;
};

static ExecutablePatternScanResult FindUniqueExecutablePattern(
	std::uintptr_t moduleBase, const unsigned char* signature,
	const bool* signatureMask, std::size_t signatureSize)
{
	ExecutablePatternScanResult result{};
	if (!moduleBase || !signature || !signatureMask || !signatureSize)
		return result;

	IMAGE_DOS_HEADER dosHeader{};
	if (!ReadMemorySafe(moduleBase, &dosHeader, sizeof(dosHeader)) ||
		dosHeader.e_magic != IMAGE_DOS_SIGNATURE || dosHeader.e_lfanew <= 0)
		return result;

	IMAGE_NT_HEADERS64 ntHeaders{};
	const std::uintptr_t ntAddress = moduleBase + static_cast<std::uintptr_t>(dosHeader.e_lfanew);
	if (!ReadMemorySafe(ntAddress, &ntHeaders, sizeof(ntHeaders)) ||
		ntHeaders.Signature != IMAGE_NT_SIGNATURE ||
		ntHeaders.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
		!ntHeaders.FileHeader.NumberOfSections || ntHeaders.FileHeader.NumberOfSections > 96 ||
		ntHeaders.FileHeader.SizeOfOptionalHeader < sizeof(ntHeaders.OptionalHeader))
		return result;

	const std::size_t imageSize = ntHeaders.OptionalHeader.SizeOfImage;
	const std::size_t sectionTableOffset = static_cast<std::size_t>(dosHeader.e_lfanew) +
		offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + ntHeaders.FileHeader.SizeOfOptionalHeader;
	const std::size_t sectionTableSize = ntHeaders.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
	if (!imageSize || sectionTableOffset > imageSize || sectionTableSize > imageSize - sectionTableOffset)
		return result;
	const std::uintptr_t sectionTable = moduleBase + sectionTableOffset;
	result.complete = true;

	for (std::size_t sectionIndex = 0; sectionIndex < ntHeaders.FileHeader.NumberOfSections; ++sectionIndex)
	{
		IMAGE_SECTION_HEADER section{};
		const std::uintptr_t sectionAddress = sectionTable + sectionIndex * sizeof(section);
		if (!ReadMemorySafe(sectionAddress, &section, sizeof(section)))
		{
			result.complete = false;
			break;
		}
		if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))
			continue;

		const std::size_t sectionSize = section.Misc.VirtualSize;
		if (sectionSize < signatureSize)
			continue;
		if (section.VirtualAddress >= imageSize || sectionSize > imageSize - section.VirtualAddress)
		{
			result.complete = false;
			break;
		}

		std::vector<unsigned char> sectionBytes;
		try
		{
			sectionBytes.resize(sectionSize);
		}
		catch (...)
		{
			result.complete = false;
			break;
		}
		if (!ReadMemorySafe(moduleBase + section.VirtualAddress, sectionBytes.data(), sectionBytes.size()))
		{
			result.complete = false;
			break;
		}

		for (std::size_t offset = 0; offset <= sectionSize - signatureSize; ++offset)
		{
			bool matches = true;
			for (std::size_t byteIndex = 0; byteIndex < signatureSize; ++byteIndex)
			{
				if (signatureMask[byteIndex] &&
					sectionBytes[offset + byteIndex] != signature[byteIndex])
				{
					matches = false;
					break;
				}
			}
			if (!matches)
				continue;

			if (!result.matchCount)
				result.address = moduleBase + section.VirtualAddress + offset;
			++result.matchCount;
		}
	}

	return result;
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

static void CallRecoilControlSafe(void* input, unsigned int slot)
{
	__try
	{
		ApplyRecoilControl(input, static_cast<int>(slot));
	}
	__except (SehDiagnostics::handle("hook.create_move.recoil_control"))
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

static bool RefreshEspCacheSafe()
{
	__try
	{
		Esp::cache();
		return true;
	}
	__except (SehDiagnostics::handle("hook.create_move.esp_cache"))
	{
		return false;
	}
}

static void hkFrameStageNotify_impl(void* a1, int stage)
{
	if (stage == 7 && H::oGetLocalPlayer)
	{
		__try
		{
			Skins::onFrameStage(stage, H::oGetLocalPlayer(0));
		}
		__except (SehDiagnostics::handle("hook.frame_stage.skins_stage7"))
		{
		}
	}

	H::FrameStageNotify.GetOriginal()(a1, stage);
	if (stage == 7 && H::oGetLocalPlayer)
	{
		__try
		{
			Skins::onFrameStagePost(stage, H::oGetLocalPlayer(0));
		}
		__except (SehDiagnostics::handle("hook.frame_stage.skins_post_stage7"))
		{
		}
	}
	if (stage == 7)
	{
		__try
		{
			H::updateRain();
		}
		__except (SehDiagnostics::handle("hook.frame_stage.weather"))
		{
		}
	}
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

	if (stage != 12)
		return;
	DebugConsole::once("frame_stage_12", "[runtime] FrameStageNotify reached stage 12");
}

std::uintptr_t __fastcall H::hkGetItemInLoadout(std::uintptr_t manager, std::uint32_t team,
	std::uint32_t loadoutSlot)
{
	const auto original = H::GetItemInLoadoutHook.GetOriginal();
	if (!original) {
		DebugConsole::rateLimited("skins.loadout_original_missing",
			"[skins] GetItemInLoadout original function is unavailable");
		return 0;
	}

	const std::uintptr_t itemView = original(manager, team, loadoutSlot);
	static std::atomic<std::uint64_t> callCount{};
	const std::uint64_t calls = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
	DebugConsole::rateLimited("skins.loadout_hook_calls",
		"[skins] GetItemInLoadout calls=%llu view=0x%llX team=%u slot=%u",
		static_cast<unsigned long long>(calls), static_cast<unsigned long long>(itemView), team, loadoutSlot);
	if (itemView) {
		__try {
			Skins::onItemInLoadout(itemView, team, loadoutSlot);
		}
		__except (SehDiagnostics::handle("hook.get_item_in_loadout.skins")) {
		}
	}
	return itemView;
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
	H::releaseRain();

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
	if (oGetLocalPlayer)
		Movement::applyBunnyHopInput(!menuOpen && Config::bunnyHop ? oGetLocalPlayer(0) : nullptr);
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

	static ULONGLONG lastEspCacheRefresh = 0;
	const ULONGLONG now = GetTickCount64();
	const bool espEnabled = Config::esp || Config::showHealth || Config::espFill ||
		Config::showNameTags || Config::flags || Config::skeleton;
	if (espEnabled && now - lastEspCacheRefresh >= 50)
	{
		lastEspCacheRefresh = now;
		if (RefreshEspCacheSafe())
			DebugConsole::once("esp.cache.called", "[runtime] ESP cache refreshed from CreateMove; cached player count=%llu",
				static_cast<unsigned long long>(cached_players.size()));
	}

	if (menuOpen)
	{
		PrepareInputSafe(input, true);
		SuppressInputSafe(input);
		return;
	}
	if (Config::aimbot)
		DebugConsole::once("aim.dispatch", "[runtime] aim dispatch reached from CreateMove; enabled=1");
	CallRecoilControlSafe(input, slot);
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
	{
		const uintptr_t engineBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("engine2.dll"));
		std::int32_t gameBuild = 0;
		if (engineBase)
			SafeMemory::read(engineBase + Offset::dwBuildNumber, gameBuild);
		if (gameBuild == Offset::kExpectedGameBuild)
			DebugConsole::logf("[offsets] game build %d matches the offsets in offsets.hpp/buttons.hpp", gameBuild);
		else
			DebugConsole::logf("[offsets] WARNING: running game build is %d but offsets are for %d - update offsets.hpp, buttons.hpp and kExpectedGameBuild",
				gameBuild, Offset::kExpectedGameBuild);
	}

	auto scan = [](const char* module, const char* pattern) -> uintptr_t {
		return M::patternScan(module, pattern);
	};
	const uintptr_t fileServiceReadPattern = scan("filesystem_stdio",
		"00 48 89 07 48 8D 05 ? ? ? ? 48 89 87 E0 00 00 00");
	const uintptr_t fileServiceReadSlot = fileServiceReadPattern
		? M::getAbsoluteAddress(fileServiceReadPattern, 0x7)
		: 0;
	uintptr_t fileServiceRead = 0;
	if (fileServiceReadSlot)
		SafeMemory::read(fileServiceReadSlot, fileServiceRead);
	InstallHook("EmbeddedParticleRead", "filesystem_stdio", fileServiceRead, FileServiceRead,
		reinterpret_cast<void*>(&hkFileServiceRead));

	const uintptr_t clientBaseForLoadout = modules.getModule("client");
	constexpr uintptr_t expectedGetItemInLoadoutRva = 0x83CB10; // Stale hint; the signature is authoritative.
	constexpr unsigned char getItemInLoadoutSignature[] = {
		0x40, 0x55, 0x48, 0x83, 0xEC, 0x00, 0x49, 0x63, 0xE8
	};
	constexpr bool getItemInLoadoutSignatureMask[] = {
		true, true, true, true, true, false, true, true, true
	};
	const ExecutablePatternScanResult loadoutPattern = FindUniqueExecutablePattern(
		clientBaseForLoadout, getItemInLoadoutSignature, getItemInLoadoutSignatureMask,
		sizeof(getItemInLoadoutSignature));
	const bool uniqueLoadoutTarget = loadoutPattern.complete && loadoutPattern.matchCount == 1 &&
		IsExecutableAddress(reinterpret_cast<void*>(loadoutPattern.address));
	DebugConsole::logf("[hook] GetItemInLoadout/skins: executable signature matches=%llu scan_complete=%d candidate=%p candidate_RVA=0x%llX supplied_RVA=0x%llX",
		static_cast<unsigned long long>(loadoutPattern.matchCount), loadoutPattern.complete ? 1 : 0,
		reinterpret_cast<void*>(loadoutPattern.address),
		static_cast<unsigned long long>(loadoutPattern.address && clientBaseForLoadout
			? loadoutPattern.address - clientBaseForLoadout : 0),
		static_cast<unsigned long long>(expectedGetItemInLoadoutRva));

	if (uniqueLoadoutTarget) {
		if (loadoutPattern.address - clientBaseForLoadout != expectedGetItemInLoadoutRva)
			DebugConsole::logf("[hook] GetItemInLoadout/skins: supplied RVA does not match; using unique executable signature at client+0x%llX",
				static_cast<unsigned long long>(loadoutPattern.address - clientBaseForLoadout));
		InstallHook("GetItemInLoadout/skins", "client", loadoutPattern.address, GetItemInLoadoutHook,
			reinterpret_cast<void*>(&hkGetItemInLoadout));
	} else {
		DebugConsole::logf("[hook] GetItemInLoadout/skins: not installed; need exactly one complete executable-section signature match");
	}

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
		"client", "85 D2 0F 85 ? ? ? ? 48 8B C4 44 88 40 18 89");
	if (!createMove)
		createMove = scan(
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
	const uintptr_t setupFog = scan("client",
		"48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 4C 24 ? 57 41 54 41 55 41 56 41 57 48 83 EC 20 48 63 02");
	InstallHook("SetupFog/Weather", "client", setupFog, SetupFog,
		reinterpret_cast<void*>(&hkSetupFog));
	const uintptr_t setPostprocessVecCall = scan(
		"engine2", "E8 ? ? ? ? 44 0F 28 94 24");
	const uintptr_t setPostprocessVec = setPostprocessVecCall
		? M::getAbsoluteAddress(setPostprocessVecCall, 0x1)
		: 0;
	InstallHook("SetPostprocessVec/NightMode", "engine2", setPostprocessVec, SetPostprocessVec,
		reinterpret_cast<void*>(&hkSetPostprocessVec));
	const uintptr_t clientBase = modules.getModule("client");
	uintptr_t frameStageNotify = scan(
		"client", "48 89 5C 24 ? 48 89 6C 24 ? 57 48 83 EC ? 48 8B F9 33 ED");
	if (!frameStageNotify)
		frameStageNotify = scan(
			"client", "48 89 5C 24 ? 56 48 83 EC ? 8B 05 ? ? ? ? 8D 5A");
	DebugConsole::logf("[scan] FrameStageNotify: %s at %p (client RVA 0x%llX)",
		frameStageNotify ? "FOUND" : "MISS", reinterpret_cast<void*>(frameStageNotify),
		static_cast<unsigned long long>(frameStageNotify && clientBase ? frameStageNotify - clientBase : 0));
	InstallHook("FrameStageNotify", "client", frameStageNotify, FrameStageNotify,
		reinterpret_cast<void*>(&hkFrameStageNotify));
	// DrawArray is DrawAggeregateObject (SDK spelling) in scenesystem.dll.
	const uintptr_t drawArray = scan("scenesystem", "48 8B C4 4C 89 40 ? 48 89 50 ? 55 53 41 57");
	InstallHook("DrawArray/DrawAggeregateObject", "scenesystem", drawArray, DrawArray,
		reinterpret_cast<void*>(&chams::hook));
	// Chams need the scene object and primitive buffer before the renderer batches
	// meshes.
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
