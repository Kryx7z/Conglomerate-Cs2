#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <cstring>
#include <Windows.h>
#include "../../hooks/hooks.h"
#include "../../config/config.h"
#include "../../interfaces/interfaces.h"
#include "../../utils/memory/patternscan/patternscan.h"
#include "../../utils/memory/gaa/gaa.h"
#include "../../utils/memory/safe_memory.h"
#include "../../../cs2/entity/C_BaseEntity/C_BaseEntity.h"

namespace
{
    struct SkyColor
    {
        std::uint8_t r, g, b, a;
    };

    struct SkyState
    {
        SkyColor tint{};
        SkyColor lighting{};
        float brightness = 1.0f;
    };

    std::unordered_map<std::uintptr_t, SkyState> originalSkyStates;

    std::uintptr_t activeSkyboxColorAddress = 0;
    float activeSkyboxOriginalColor[3]{};
}

struct C_ByteColor4
{
	std::uint8_t r, g, b, a;
};

namespace
{
    static std::uint8_t colorByte(float component)
    {
        component = std::clamp(component, 0.0f, 1.0f);
        return static_cast<std::uint8_t>(component * 255.0f);
    }

    static C_ByteColor4 configuredNightColor()
    {
        return {
            colorByte(Config::NightColor.x),
            colorByte(Config::NightColor.y),
            colorByte(Config::NightColor.z),
            255
        };
    }

    template <typename T>
    static bool readMemory(std::uintptr_t address, T& value) noexcept
    {
        return SafeMemory::read(address, value);
    }

    template <typename T>
    static bool writeMemory(std::uintptr_t address, const T& value) noexcept
    {
        return SafeMemory::write(address, value);
    }

    static std::uintptr_t resolveLightDataQueueGlobal()
	{
		static const auto address = M::getAbsoluteAddress(
			M::patternScan("scenesystem", "48 8B 05 ? ? ? ? 48 C1 E1 04"), 0x3, 0x8);
		return address;
	}

	static bool readSceneBatch(c_aggregate_object_array* objects, std::uintptr_t& lightBase,
		int& count, int& startIndex)
	{
		if (!objects)
			return false;

		const auto objectArray = reinterpret_cast<std::uintptr_t>(objects);
		std::uintptr_t objectData = 0;
		if (!readMemory(objectArray + 0x8u, objectData) || !objectData)
			return false;

		const auto queueGlobal = resolveLightDataQueueGlobal();
		std::uintptr_t queue = 0;
		if (queueGlobal)
			readMemory(queueGlobal, queue);

		if (!queue && I::SceneSystem)
		{
			readMemory(reinterpret_cast<std::uintptr_t>(I::SceneSystem) +
				offsetof(ISceneSystem, light_data_queue), queue);
		}
		if (!queue || !readMemory(queue + 0x18u, lightBase) || !lightBase)
			return false;

		if (!readMemory(objectData + 0x4u, count) ||
			!readMemory(objectData + 0x30u, startIndex))
			return false;

		return count > 0 && count <= (1 << 20) && startIndex >= 0 && startIndex <= (1 << 22);
	}

	static bool readSceneColor(std::uintptr_t address, C_ByteColor4& color)
	{
		return readMemory(address, color);
	}

	static bool writeSceneColor(std::uintptr_t address, const C_ByteColor4& color) noexcept
	{
		return writeMemory(address, color);
	}

	static bool isSkyOverlayMaterial(std::uintptr_t material)
	{
		if (!material)
			return false;

		const char* name = nullptr;
		__try
		{
			name = reinterpret_cast<CMaterial2*>(material)->GetName();
		}
		__except (SehDiagnostics::handle("world.material_name"))
		{
			return false;
		}

		if (!name)
			return false;

		bool isOverlay = false;
		__try
		{
			isOverlay = std::strstr(name, "cloud") != nullptr ||
				std::strstr(name, "sun") != nullptr;
		}
		__except (SehDiagnostics::handle("world.material_string"))
		{
			return false;
		}
		return isOverlay;
	}
}

static void applyNightModeColoring(c_aggregate_object_array* objects)
{
	if (!Config::Night)
		return;

	std::uintptr_t lightBase = 0;
	int count = 0;
	int startIndex = 0;
	if (!readSceneBatch(objects, lightBase, count, startIndex))
		return;

	const C_ByteColor4 configured = configuredNightColor();

	for (int i = 0; i < count; ++i)
	{
		const auto address = lightBase + (static_cast<std::uintptr_t>(startIndex + i) << 5);
		C_ByteColor4 current{};
		if (readSceneColor(address, current))
			writeSceneColor(address, C_ByteColor4{ configured.r, configured.g, configured.b, current.a });
	}
}

void __fastcall H::hkUpdateSceneObject(void* a1, void* a2, c_aggregate_object_array* a3)
{
	static auto original = H::UpdateWallsObject.GetOriginal();
	if (!original)
		return;

	// Always call the real game function first and keep its result. This
	// must never be skipped or lost, or the game itself could misbehave.
	original(a1, a2, a3);
	applyNightModeColoring(a3);
}

static bool readSkyState(std::uintptr_t address, std::uint32_t tintOffset,
    std::uint32_t lightingOffset, std::uint32_t brightnessOffset, SkyState& out)
{
    return readMemory(address + tintOffset, out.tint) &&
        readMemory(address + lightingOffset, out.lighting) &&
        readMemory(address + brightnessOffset, out.brightness);
}

static bool writeSkyState(std::uintptr_t address, std::uint32_t tintOffset,
    std::uint32_t lightingOffset, std::uint32_t brightnessOffset,
    const SkyColor& tint, const SkyColor& lighting, float brightness)
{
    return writeMemory(address + tintOffset, tint) &&
        writeMemory(address + lightingOffset, lighting) &&
        writeMemory(address + brightnessOffset, brightness);
}

static bool getClassInfoSafe(C_BaseEntity* entity, SchemaClassInfoData_t*& classInfo)
{
    if (!entity)
        return false;

    __try
    {
        entity->dump_class_info(&classInfo);
        return true;
    }
    __except (SehDiagnostics::handle("world.class_info"))
    {
        return false;
    }
}

static bool isSkyEntity(C_BaseEntity* entity)
{
    SchemaClassInfoData_t* classInfo = nullptr;
    if (!getClassInfoSafe(entity, classInfo))
        return false;

    return classInfo && classInfo->szName &&
        hash_32_fnv1a_const(classInfo->szName) == hash_32_fnv1a_const("C_EnvSky");
}

static bool prepareSkyboxColor(void* meshArray, int meshCount)
{
    if (!meshArray || meshCount <= 0 || meshCount > (1 << 20))
        return false;

    const auto meshBase = reinterpret_cast<std::uintptr_t>(meshArray);
    std::uintptr_t descriptor = 0;
    if (!readMemory(meshBase + (static_cast<std::uintptr_t>(meshCount) * 0x70u) - 0x58u, descriptor))
        return false;
    if (descriptor < 0x10000u || descriptor > 0x00007FFFFFFFFFFFull)
        return false;

    activeSkyboxColorAddress = descriptor + 0xE8u;
    if (!readMemory(activeSkyboxColorAddress + 0x0u, activeSkyboxOriginalColor[0]) ||
        !readMemory(activeSkyboxColorAddress + 0x4u, activeSkyboxOriginalColor[1]) ||
        !readMemory(activeSkyboxColorAddress + 0x8u, activeSkyboxOriginalColor[2]))
    {
        activeSkyboxColorAddress = 0;
        return false;
    }

    const float nightColor[3] = {
        12.0f / 255.0f,
        24.0f / 255.0f,
        55.0f / 255.0f
    };
    if (!writeMemory(activeSkyboxColorAddress + 0x0u, nightColor[0]) ||
        !writeMemory(activeSkyboxColorAddress + 0x4u, nightColor[1]) ||
        !writeMemory(activeSkyboxColorAddress + 0x8u, nightColor[2]))
    {
        activeSkyboxColorAddress = 0;
        return false;
    }

    return true;
}

static void restoreSkyboxColor()
{
    if (!activeSkyboxColorAddress)
        return;

    writeMemory(activeSkyboxColorAddress + 0x0u, activeSkyboxOriginalColor[0]);
    writeMemory(activeSkyboxColorAddress + 0x4u, activeSkyboxOriginalColor[1]);
    writeMemory(activeSkyboxColorAddress + 0x8u, activeSkyboxOriginalColor[2]);

    activeSkyboxColorAddress = 0;
}

static void applyNightLightColor(void* object)
{
	if (!Config::Night || !object)
		return;

	const auto address = reinterpret_cast<std::uintptr_t>(object);
	writeMemory(address + 0xE4u, Config::NightColor.x);
	writeMemory(address + 0xE8u, Config::NightColor.y);
	writeMemory(address + 0xECu, Config::NightColor.z);
}

static void applyNightPrimitiveColor(void* batch, int batchCount)
{
	if (!Config::Night || !batch || batchCount <= 0 || batchCount > (1 << 20))
		return;

	const C_ByteColor4 configured = configuredNightColor();

	const auto base = reinterpret_cast<std::uintptr_t>(batch);
	std::uintptr_t cachedMaterial = 0;
	bool cachedOverlay = false;
	for (int i = 0; i < batchCount; ++i)
	{
		const auto mesh = base + (static_cast<std::uintptr_t>(i) * 0x70u);
		std::uintptr_t material = 0;
		if (!readMemory(mesh + 0x20u, material))
			continue;

		if (material != cachedMaterial)
		{
			cachedMaterial = material;
			cachedOverlay = isSkyOverlayMaterial(material);
		}
		if (cachedOverlay)
			continue;

		const auto colorAddress = mesh + 0x50u;
		writeSceneColor(colorAddress, configured);
	}
}

std::uintptr_t __fastcall H::hkUpdateLightObject(void* a1, void* a2, void* a3)
{
	static auto original = H::UpdateLightObject.GetOriginal();
	if (!original)
		return 0;

	applyNightLightColor(a2);
	return original(a1, a2, a3);
}

std::uintptr_t __fastcall H::hkDrawSceneObject(void* a1, void* a2, void* batch,
	int batchCount, int a5, void* a6, void* a7, void* a8)
{
	static auto original = H::DrawSceneObject.GetOriginal();
	if (!original)
		return 0;

	applyNightPrimitiveColor(batch, batchCount);
	return original(a1, a2, batch, batchCount, a5, a6, a7, a8);
}

void H::updateSky()
{
    if (!I::GameEntity || !I::GameEntity->Instance)
        return;

    static std::uint32_t tintOffset = 0;
    static std::uint32_t lightingOffset = 0;
    static std::uint32_t brightnessOffset = 0;
    static std::uint64_t nextSkyScan = 0;

    if (!tintOffset || !lightingOffset || !brightnessOffset)
    {
        tintOffset = SchemaFinder::Get("C_EnvSky->m_vTintColor");
        lightingOffset = SchemaFinder::Get("C_EnvSky->m_vTintColorLightingOnly");
        brightnessOffset = SchemaFinder::Get("C_EnvSky->m_flBrightnessScale");
    }

    if (!tintOffset || !lightingOffset || !brightnessOffset)
        return;

    if (!Config::Night)
    {
        for (auto it = originalSkyStates.begin(); it != originalSkyStates.end();)
        {
            if (writeSkyState(it->first, tintOffset, lightingOffset, brightnessOffset,
                it->second.tint, it->second.lighting, it->second.brightness))
                it = originalSkyStates.erase(it);
            else
                ++it;
        }

        nextSkyScan = 0;
        return;
    }

    const std::uint64_t now = GetTickCount64();
    if (now >= nextSkyScan)
    {
        const int highest = I::GameEntity->Instance->GetHighestEntityIndex();
        for (int i = 1; i <= highest; ++i)
        {
            auto* entity = I::GameEntity->Instance->Get(i);
            if (!isSkyEntity(entity))
                continue;

            const auto address = reinterpret_cast<std::uintptr_t>(entity);
            if (originalSkyStates.contains(address))
                continue;

            SkyState original{};
            if (readSkyState(address, tintOffset, lightingOffset, brightnessOffset, original))
                originalSkyStates.emplace(address, original);
        }

        nextSkyScan = now + 500;
    }

    const SkyColor color{ 12, 24, 55, 255 };
    for (const auto& [address, original] : originalSkyStates)
    {
        writeSkyState(address, tintOffset, lightingOffset, brightnessOffset,
            color, color, std::clamp(original.brightness * 0.25f, 0.05f, 1.0f));
    }
}

void H::updateSmoke()
{
    if (!Config::noSmoke || !I::GameEntity || !I::GameEntity->Instance)
        return;

    static std::uint32_t smokeTickOffset = 0;
    static std::uint32_t didSmokeOffset = 0;
    if (!smokeTickOffset)
    {
        const auto offset = SchemaFinder::Get("C_SmokeGrenadeProjectile->m_nSmokeEffectTickBegin");
        smokeTickOffset = offset ? offset : SchemaFinder::Get("CSmokeGrenadeProjectile->m_nSmokeEffectTickBegin");
    }
    if (!didSmokeOffset)
    {
        const auto offset = SchemaFinder::Get("C_SmokeGrenadeProjectile->m_bDidSmokeEffect");
        didSmokeOffset = offset ? offset : SchemaFinder::Get("CSmokeGrenadeProjectile->m_bDidSmokeEffect");
    }
    if (!smokeTickOffset && !didSmokeOffset)
        return;

    const int highest = I::GameEntity->Instance->GetHighestEntityIndex();
    for (int i = 1; i <= highest; ++i)
    {
        auto* entity = I::GameEntity->Instance->Get(i);
        SchemaClassInfoData_t* classInfo = nullptr;
        if (!getClassInfoSafe(entity, classInfo) || !classInfo || !classInfo->szName)
            continue;

        const auto hash = hash_32_fnv1a_const(classInfo->szName);
        if (hash != hash_32_fnv1a_const("C_SmokeGrenadeProjectile") &&
            hash != hash_32_fnv1a_const("CSmokeGrenadeProjectile"))
            continue;

        const auto address = reinterpret_cast<std::uintptr_t>(entity);
        if (smokeTickOffset)
            writeMemory(address + smokeTickOffset, -1);
        if (didSmokeOffset)
        {
            const bool didSmoke = true;
            writeMemory(address + didSmokeOffset, didSmoke);
        }
    }
}

void __fastcall H::hkDrawSkyboxArray(void* a1, void* a2, void* meshArray, int meshCount,
    void* a5, void* a6, void* a7, void* a8)
{
    static auto original = H::DrawSkyboxArray.GetOriginal();
    if (!original)
        return;

    const bool tinted = Config::Night && prepareSkyboxColor(meshArray, meshCount);
    original(a1, a2, meshArray, meshCount, a5, a6, a7, a8);

	if (tinted)
		restoreSkyboxColor();
}
