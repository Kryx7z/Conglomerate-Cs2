#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <cstring>
#include <Windows.h>
#include "../../hooks/hooks.h"
#include "../../players/players.h"
#include "../../config/config.h"
#include "../../../../external/imgui/imgui.h"
#include "../../interfaces/interfaces.h"
#include "../../utils/memory/patternscan/patternscan.h"
#include "../../utils/memory/gaa/gaa.h"
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
    template <typename T>
    static bool readMemory(std::uintptr_t address, T& value)
    {
        if (!address)
            return false;

        SIZE_T bytesRead = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
            &value, sizeof(T), &bytesRead) != FALSE && bytesRead == sizeof(T);
    }

    template <typename T>
    static bool writeMemory(std::uintptr_t address, const T& value)
    {
        if (!address)
            return false;

        SIZE_T bytesWritten = 0;
        return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
            &value, sizeof(T), &bytesWritten) != FALSE && bytesWritten == sizeof(T);
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

		// Match Velocity's aggregate layout directly. The third argument is an
		// object-array pointer; its data pointer is at +0x8, count at +0x4 and
		// the light-record start index at +0x30.
		const auto objectArray = reinterpret_cast<std::uintptr_t>(objects);
		std::uintptr_t objectData = 0;
		if (!readMemory(objectArray + 0x8u, objectData) || !objectData)
			return false;

		const auto queueGlobal = resolveLightDataQueueGlobal();
		std::uintptr_t queue = 0;
		if (queueGlobal)
			readMemory(queueGlobal, queue);

		// Keep the interface as a fallback for builds where the global pattern
		// is not present.
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

	static void writeSceneColor(std::uintptr_t address, const C_ByteColor4& color)
	{
		(void)writeMemory(address, color);
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
		__except (EXCEPTION_EXECUTE_HANDLER)
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
		__except (EXCEPTION_EXECUTE_HANDLER)
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

	const C_ByteColor4 configured{
		static_cast<std::uint8_t>(std::clamp(Config::NightColor.x * 255.0f, 0.0f, 255.0f)),
		static_cast<std::uint8_t>(std::clamp(Config::NightColor.y * 255.0f, 0.0f, 255.0f)),
		static_cast<std::uint8_t>(std::clamp(Config::NightColor.z * 255.0f, 0.0f, 255.0f)),
		255
	};

	// Match Velocity: replace RGB with the configured world color after the
	// engine has filled the aggregate records, while retaining each record's
	// alpha. This is idempotent and does not progressively remove contrast.
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

	// Always call the real game function first and keep its result - this
	// must never be skipped or lost, or the game itself could misbehave.
	original(a1, a2, a3);
	applyNightModeColoring(a3);
}

static bool readSkyState(std::uintptr_t address, std::uint32_t tintOffset,
    std::uint32_t lightingOffset, std::uint32_t brightnessOffset, SkyState& out)
{
    __try
    {
        out.tint = *reinterpret_cast<SkyColor*>(address + tintOffset);
        out.lighting = *reinterpret_cast<SkyColor*>(address + lightingOffset);
        out.brightness = *reinterpret_cast<float*>(address + brightnessOffset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool writeSkyState(std::uintptr_t address, std::uint32_t tintOffset,
    std::uint32_t lightingOffset, std::uint32_t brightnessOffset,
    const SkyColor& tint, const SkyColor& lighting, float brightness)
{
    __try
    {
        *reinterpret_cast<SkyColor*>(address + tintOffset) = tint;
        *reinterpret_cast<SkyColor*>(address + lightingOffset) = lighting;
        *reinterpret_cast<float*>(address + brightnessOffset) = brightness;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool isSkyEntity(C_BaseEntity* entity)
{
    __try
    {
        if (!entity)
            return false;

        SchemaClassInfoData_t* classInfo = nullptr;
        entity->dump_class_info(&classInfo);
        return classInfo && classInfo->szName &&
            hash_32_fnv1a_const(classInfo->szName) == hash_32_fnv1a_const("C_EnvSky");
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool prepareSkyboxColor(void* meshArray, int meshCount)
{
    __try
    {
        if (!meshArray || meshCount <= 0 || meshCount > (1 << 20))
            return false;

        const auto meshBase = reinterpret_cast<std::uintptr_t>(meshArray);
        const auto descriptor = *reinterpret_cast<std::uintptr_t*>(
            meshBase + (static_cast<std::uintptr_t>(meshCount) * 0x70u) - 0x58u);
        if (descriptor < 0x10000u || descriptor > 0x00007FFFFFFFFFFFull)
            return false;

        activeSkyboxColorAddress = descriptor + 0xE8u;
        auto* color = reinterpret_cast<float*>(activeSkyboxColorAddress);
        activeSkyboxOriginalColor[0] = color[0];
        activeSkyboxOriginalColor[1] = color[1];
        activeSkyboxOriginalColor[2] = color[2];

        // Match the dark-blue sky used by the intended night-mode look.
        color[0] = 12.0f / 255.0f;
        color[1] = 24.0f / 255.0f;
        color[2] = 55.0f / 255.0f;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        activeSkyboxColorAddress = 0;
        return false;
    }
}

static void restoreSkyboxColor()
{
    __try
    {
        if (!activeSkyboxColorAddress)
            return;

        auto* color = reinterpret_cast<float*>(activeSkyboxColorAddress);
        color[0] = activeSkyboxOriginalColor[0];
        color[1] = activeSkyboxOriginalColor[1];
        color[2] = activeSkyboxOriginalColor[2];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    activeSkyboxColorAddress = 0;
}

static void applyNightLightColor(void* object)
{
	if (!Config::Night || !object)
		return;

	__try
	{
		const auto address = reinterpret_cast<std::uintptr_t>(object);
		*reinterpret_cast<float*>(address + 0xE4u) = Config::NightColor.x;
		*reinterpret_cast<float*>(address + 0xE8u) = Config::NightColor.y;
		*reinterpret_cast<float*>(address + 0xECu) = Config::NightColor.z;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

static void applyNightPrimitiveColor(void* batch, int batchCount)
{
	if (!Config::Night || !batch || batchCount <= 0 || batchCount > (1 << 20))
		return;

	const C_ByteColor4 configured{
		static_cast<std::uint8_t>(std::clamp(Config::NightColor.x * 255.0f, 0.0f, 255.0f)),
		static_cast<std::uint8_t>(std::clamp(Config::NightColor.y * 255.0f, 0.0f, 255.0f)),
		static_cast<std::uint8_t>(std::clamp(Config::NightColor.z * 255.0f, 0.0f, 255.0f)),
		255
	};

	const auto base = reinterpret_cast<std::uintptr_t>(batch);
	for (int i = 0; i < batchCount; ++i)
	{
		// Velocity's draw_scene_object path uses 0x70-byte scene primitives,
		// reads their material at +0x20 and writes packed color at +0x50.
		const auto mesh = base + (static_cast<std::uintptr_t>(i) * 0x70u);
		std::uintptr_t material = 0;
		if (!readMemory(mesh + 0x20u, material) || isSkyOverlayMaterial(material))
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

	// Match Velocity: apply the primitive color before the real renderer sees
	// the batch, so the sky/world materials are shaded consistently.
	applyNightPrimitiveColor(batch, batchCount);
	return original(a1, a2, batch, batchCount, a5, a6, a7, a8);
}

void H::updateSky()
{
    if (!I::GameEntity || !I::GameEntity->Instance)
        return;

    const auto tintOffset = SchemaFinder::Get("C_EnvSky->m_vTintColor");
    const auto lightingOffset = SchemaFinder::Get("C_EnvSky->m_vTintColorLightingOnly");
    const auto brightnessOffset = SchemaFinder::Get("C_EnvSky->m_flBrightnessScale");
    if (!tintOffset || !lightingOffset || !brightnessOffset)
        return;

    const int highest = I::GameEntity->Instance->GetHighestEntityIndex();
    for (int i = 1; i <= highest; ++i)
    {
        auto* entity = I::GameEntity->Instance->Get(i);
        if (!isSkyEntity(entity))
            continue;

        const auto address = reinterpret_cast<std::uintptr_t>(entity);

        if (Config::Night)
        {
            SkyState current{};
            if (!originalSkyStates.contains(address))
            {
                if (!readSkyState(address, tintOffset, lightingOffset, brightnessOffset, current))
                    continue;

                originalSkyStates.emplace(address, current);
            }

            // Tint both sky fields; the lighting-only field must change too or
            // the sky stays bright after the scene light queue is darkened.
            const SkyColor color{ 12, 24, 55, 255 };
            const auto original = originalSkyStates.find(address);
            if (original != originalSkyStates.end())
            {
                writeSkyState(address, tintOffset, lightingOffset, brightnessOffset,
                    color, color, std::clamp(original->second.brightness * 0.25f, 0.05f, 1.0f));
            }
        }
        else
        {
            const auto it = originalSkyStates.find(address);
            if (it != originalSkyStates.end())
            {
                if (writeSkyState(address, tintOffset, lightingOffset, brightnessOffset,
                    it->second.tint, it->second.lighting, it->second.brightness))
                {
                    originalSkyStates.erase(it);
                }
            }
        }
    }
}

void H::updateSmoke()
{
    if (!Config::noSmoke || !I::GameEntity || !I::GameEntity->Instance)
        return;

    // The live entity is commonly named C_SmokeGrenadeProjectile while the
    // reflected schema class is exported as CSmokeGrenadeProjectile. Resolve
    // both spellings so a schema rewrite cannot silently disable suppression.
    const auto smokeTickOffset = [] {
        const auto offset = SchemaFinder::Get("C_SmokeGrenadeProjectile->m_nSmokeEffectTickBegin");
        return offset ? offset : SchemaFinder::Get("CSmokeGrenadeProjectile->m_nSmokeEffectTickBegin");
    }();
    const auto didSmokeOffset = [] {
        const auto offset = SchemaFinder::Get("C_SmokeGrenadeProjectile->m_bDidSmokeEffect");
        return offset ? offset : SchemaFinder::Get("CSmokeGrenadeProjectile->m_bDidSmokeEffect");
    }();
    if (!smokeTickOffset && !didSmokeOffset)
        return;

    __try
    {
        const int highest = I::GameEntity->Instance->GetHighestEntityIndex();
        for (int i = 1; i <= highest; ++i)
        {
            auto* entity = I::GameEntity->Instance->Get(i);
            if (!entity)
                continue;

            SchemaClassInfoData_t* classInfo = nullptr;
            entity->dump_class_info(&classInfo);
            if (!classInfo || !classInfo->szName)
                continue;

            const auto hash = hash_32_fnv1a_const(classInfo->szName);
            if (hash != hash_32_fnv1a_const("C_SmokeGrenadeProjectile") &&
                hash != hash_32_fnv1a_const("CSmokeGrenadeProjectile"))
                continue;

            const auto address = reinterpret_cast<std::uintptr_t>(entity);
            if (smokeTickOffset)
                *reinterpret_cast<int*>(address + smokeTickOffset) = -1;
            if (didSmokeOffset)
                *reinterpret_cast<bool*>(address + didSmokeOffset) = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
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
