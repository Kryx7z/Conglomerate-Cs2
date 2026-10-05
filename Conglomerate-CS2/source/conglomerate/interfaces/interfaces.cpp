#include "interfaces.h"
#include "CGameEntitySystem/CGameEntitySystem.h"
#include "../utils/debug_console.h"

// @used: I::Get<template>
#include "..\..\conglomerate\utils\memory\Interface\Interface.h"
#include "..\..\conglomerate\utils\memory\gaa\gaa.h"

namespace
{
    bool is_readable_address(const void* address)
    {
        if (!address)
            return false;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(address, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT)
            return false;

        const DWORD protection = mbi.Protect & 0xFFu;
        return protection == PAGE_READONLY || protection == PAGE_READWRITE ||
            protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    }

    constexpr const char* createMaterialPatterns[] =
    {
        // Current CS2 materialsystem2.dll builds.
        "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 8B F2",
        // Older builds.
        "48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 81 EC ? ? ? ? 48 8B 05",
    };

    FARPROC resolve_export(const char* name)
    {
        // Valve has moved these exports between tier0.dll and client.dll
        // across recent CS2 builds. Resolve the symbol by name instead of
        // assuming one module owns it forever.
        constexpr const char* modules[] = { "client.dll", "tier0.dll" };

        for (const char* moduleName : modules)
        {
            const HMODULE module = GetModuleHandleA(moduleName);
            if (!module)
                continue;

            if (const FARPROC address = GetProcAddress(module, name))
            {
                return address;
            }
        }

        return nullptr;
    }
}

bool I::Interfaces::init()
{
    const HMODULE tier0_base = GetModuleHandleA("tier0.dll");
    if (!tier0_base)
	{
		DebugConsole::logf("[interfaces] tier0.dll is not loaded");
        return false;
	}

	bool success = true;
	constexpr bool enableInputFeatures = true;

    // interfaces
    EngineClient = I::Get<IEngineClient>(("engine2.dll"), "Source2EngineToClient001");
    success &= (EngineClient != nullptr);

    GameEntity = I::Get<IGameResourceService>(("engine2.dll"), "GameResourceServiceClientV001");
    success &= (GameEntity != nullptr);

	InputSystem = I::Get<void>("inputsystem.dll", "InputSystemVersion001");

	uintptr_t inputPattern = 0;
	if (enableInputFeatures)
	{
		inputPattern = M::patternScan(
			"client",
			"48 8B 0D ? ? ? ? 4C 8D 47 14"
		);
		if (!inputPattern)
		{
			InputUsesLegacyLayout = true;
			inputPattern = M::patternScan(
				"client",
				"48 8B 0D ? ? ? ? 8B D3 E8 ? ? ? ? ? ? ? ? F2 0F 11 45"
			);
		}
		if (inputPattern)
		{
			const uintptr_t inputGlobal = M::getAbsoluteAddress(inputPattern, 0x3);
			if (is_readable_address(reinterpret_cast<const void*>(inputGlobal)))
				Input = *reinterpret_cast<void**>(inputGlobal);
		}

		if (Input && !is_readable_address(Input))
			Input = nullptr;
		success &= (Input != nullptr);
	}

    SceneSystem = I::Get<ISceneSystem>(("scenesystem.dll"), "SceneSystem_002");
    if (!SceneSystem)
        SceneSystem = I::Get<ISceneSystem>(("scenesystem.dll"), "SceneSystem_001");
    success &= (SceneSystem != nullptr);
    MaterialSystem2 = I::Get<void>("materialsystem2.dll", "VMaterialSystem2_001");

    // exports
    ConstructUtlBuffer = reinterpret_cast<decltype(ConstructUtlBuffer)>(resolve_export("??0CUtlBuffer@@QEAA@HHW4BufferFlags_t@0@@Z"));
    if (!ConstructUtlBuffer)
        ConstructUtlBuffer = reinterpret_cast<decltype(ConstructUtlBuffer)>(resolve_export("??0CUtlBuffer@@QEAA@HHH@Z"));
    EnsureCapacityBuffer = reinterpret_cast<decltype(EnsureCapacityBuffer)>(resolve_export("?EnsureCapacity@CUtlBuffer@@QEAAXH@Z"));
    PutUtlString = reinterpret_cast<decltype(PutUtlString)>(resolve_export("?PutString@CUtlBuffer@@QEAAXPEBD@Z"));
    for (const char* pattern : createMaterialPatterns)
    {
        CreateMaterial = reinterpret_cast<decltype(CreateMaterial)>(M::FindPattern("materialsystem2.dll", pattern));
        if (CreateMaterial)
            break;
    }
    LoadKV3TextExport = reinterpret_cast<decltype(LoadKV3TextExport)>(resolve_export("?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z"));
    LoadKV3Export = reinterpret_cast<decltype(LoadKV3Export)>(resolve_export("?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEAVCUtlBuffer@@AEBUKV3ID_t@@PEBDI@Z"));
    LoadKeyValues = reinterpret_cast<decltype(LoadKeyValues)>(resolve_export("?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2@Z"));
	DebugConsole::logf("[interfaces] engine=%p entity_service=%p input=%p input_pattern=%p scene=%p material_system=%p create_material=%p exports(buffer=%p ensure=%p put=%p kv3_text=%p kv3_buffer=%p kv3=%p)",
		static_cast<void*>(EngineClient), static_cast<void*>(GameEntity), Input,
		reinterpret_cast<void*>(inputPattern), static_cast<void*>(SceneSystem), MaterialSystem2,
		reinterpret_cast<void*>(CreateMaterial), reinterpret_cast<void*>(ConstructUtlBuffer),
		reinterpret_cast<void*>(EnsureCapacityBuffer), reinterpret_cast<void*>(PutUtlString),
		reinterpret_cast<void*>(LoadKV3TextExport), reinterpret_cast<void*>(LoadKV3Export),
		reinterpret_cast<void*>(LoadKeyValues));
	DebugConsole::logf("[interfaces] required interface status=%s", success ? "ready" : "one or more required interfaces missing");

	// return status
    return success;
}
