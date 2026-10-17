#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <cstring>
#include <Windows.h>
#include "../../hooks/hooks.h"
#include "../../config/config.h"
#include "../../interfaces/interfaces.h"
#include "../../utils/memory/patternscan/patternscan.h"
#include "../../utils/memory/gaa/gaa.h"
#include "../../utils/memory/safe_memory.h"
#include "../../utils/debug_console.h"
#include "../../utils/module/module.h"
#include "../../utils/schema/schema.h"
#include "../../offsets/offsets.hpp"
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
    struct WeatherVector3
    {
        float x, y, z;
    };

    struct WeatherParticleTransform
    {
        float px, py, pz, pw;
        float qx, qy, qz, qw;
    };

    struct WeatherParticlePathBuffer
    {
        std::uint32_t unknown1{};
        std::uint32_t unknown2{ 0xc00000c8 };
        union
        {
            std::uintptr_t stringPointer;
            std::uint8_t data[0xc8];
        };
        std::uintptr_t unknown3{};
        std::uintptr_t unknown4{};
    };

    static_assert(sizeof(WeatherParticleTransform) == 0x20);

    constexpr int kInvalidWeatherEffect = -1;
    int weatherEffectIndex = kInvalidWeatherEffect;

    static std::uintptr_t weatherFunction(const char* module, const char* signature)
    {
        return M::patternScan(module, signature);
    }

    static std::uintptr_t weatherParticleManager()
    {
        static std::uintptr_t globalAddress = 0;
        if (!globalAddress)
        {
            const auto match = weatherFunction("client", "48 8B 35 ? ? ? ? 44 89 6C 24 ?");
            globalAddress = M::getAbsoluteAddress(match, 0x3);
        }

        std::uintptr_t manager = 0;
        SafeMemory::read(globalAddress, manager);
        return manager;
    }

    static std::uintptr_t weatherResourceSystem()
    {
        static bool resolved = false;
        static std::uintptr_t resourceSystem = 0;
        if (resolved)
            return resourceSystem;

        const HMODULE module = GetModuleHandleA("resourcesystem.dll");
        if (!module)
            return 0;

        resolved = true;
        using CreateInterfaceFn = void* (__cdecl*)(const char*, int*);
        const auto createInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
        if (createInterface)
            resourceSystem = reinterpret_cast<std::uintptr_t>(createInterface("ResourceSystem013", nullptr));
        return resourceSystem;
    }

    static void releaseWeatherRain()
    {
        if (weatherEffectIndex == kInvalidWeatherEffect)
            return;

        static const auto destroyEffect = weatherFunction("client", "83 FA FF 0F 84 ? ? ? ? 41 54");
        const auto manager = weatherParticleManager();
        if (manager && destroyEffect)
        {
            using DestroyEffectFn = void (__fastcall*)(std::uintptr_t, std::uint32_t, bool, bool);
            reinterpret_cast<DestroyEffectFn>(destroyEffect)(manager,
                static_cast<std::uint32_t>(weatherEffectIndex), true, true);
        }
        weatherEffectIndex = kInvalidWeatherEffect;
    }

    static bool createWeatherRain()
    {
        static const auto initPathBuffer = weatherFunction("client",
            "48 89 5C 24 ? 57 48 83 EC ? 8B 41 ? 48 8D 79");
        static const auto loadResource = weatherFunction("resourcesystem",
            "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B 01");
        static const auto createEffect = weatherFunction("client",
            "4C 8B DC 53 48 81 EC 90 00 00 00 F2 0F 10 05");

        const auto manager = weatherParticleManager();
        const auto resourceSystem = weatherResourceSystem();
        if (!manager || !resourceSystem || !initPathBuffer || !loadResource || !createEffect)
        {
            DebugConsole::rateLimited("weather.rain.dependencies",
                "[weather] rain dependencies: manager=%p resources=%p init=%p load=%p create=%p",
                reinterpret_cast<void*>(manager), reinterpret_cast<void*>(resourceSystem),
                reinterpret_cast<void*>(initPathBuffer), reinterpret_cast<void*>(loadResource),
                reinterpret_cast<void*>(createEffect));
            return false;
        }

        constexpr const char* path = "particles/embedded/rain.vpcf";
        WeatherParticlePathBuffer buffer{};
        using InitPathBufferFn = void (__fastcall*)(WeatherParticlePathBuffer*, const char*);
        using LoadResourceFn = void (__fastcall*)(std::uintptr_t, WeatherParticlePathBuffer*, const char*);
        reinterpret_cast<InitPathBufferFn>(initPathBuffer)(&buffer, path);
        buffer.unknown4 = 'fcpv';
        reinterpret_cast<LoadResourceFn>(loadResource)(resourceSystem, &buffer, "");

        using CreateEffectFn = int* (__fastcall*)(std::uintptr_t, int*, const char*, int,
            std::int64_t, std::int64_t, std::int64_t, int);
        reinterpret_cast<CreateEffectFn>(createEffect)(manager, &weatherEffectIndex, path,
            8, 0, 0, 0, 0);
        if (weatherEffectIndex != kInvalidWeatherEffect)
            DebugConsole::once("weather.rain.created",
                "[weather] rain particle effect created: index=%d manager=%p", weatherEffectIndex,
                reinterpret_cast<void*>(manager));
        else
            DebugConsole::rateLimited("weather.rain.create_failed",
                "[weather] particle manager rejected rain effect creation");
        return weatherEffectIndex != kInvalidWeatherEffect;
    }

    static void applyWeatherWind(std::uint32_t hash, __m128i*& value)
    {
        constexpr std::uint32_t windDirectionHash = 0x2A416C12;
        constexpr std::uint32_t windStrengthFrequencyHash = 0xEB0D997E;
        static __m128 windDirection{};
        static __m128 windStrengthFrequency{};
        if (!Config::weatherWindEnabled)
            return;

        if (hash == windDirectionHash)
        {
            constexpr float radiansPerDegree = 0.017453292519943295f;
            const float direction = Config::weatherWindDirection * radiansPerDegree;
            windDirection = _mm_set_ps(0.0f, 0.0f, std::sin(direction), std::cos(direction));
            value = reinterpret_cast<__m128i*>(&windDirection);
        }
        else if (hash == windStrengthFrequencyHash)
        {
            windStrengthFrequency = _mm_set_ps(Config::weatherWindTurbulence,
                Config::weatherWindStrength, Config::weatherWindTurbulence, Config::weatherWindStrength);
            value = reinterpret_cast<__m128i*>(&windStrengthFrequency);
        }
    }

    static bool applyWeatherFog(__m128i* output, int* mode)
    {
        if (!Config::weatherFogEnabled || !output || !mode)
            return false;

        const auto setShaderParam = H::SetShaderParam.GetOriginal();
        static const auto setShaderParamInt = weatherFunction("client",
            "48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 66 0F 6E CA 41 8B F0");
        if (!setShaderParam || !setShaderParamInt)
        {
            DebugConsole::rateLimited("weather.fog.functions",
                "[weather] fog enabled but shader setters are unavailable: vec=%p int=%p",
                reinterpret_cast<void*>(setShaderParam), reinterpret_cast<void*>(setShaderParamInt));
            return false;
        }

        constexpr std::uint32_t fogDistanceHash = 0x4B01FF63;
        constexpr std::uint32_t fogDensityHash = 0x0AA49C2A;
        constexpr std::uint32_t fogColorHash = 0xFBF6448D;
        constexpr std::uint32_t fogEnabledHash = 0x6E0FAD7E;
        const float falloff = 0.5f + Config::weatherFogAnisotropy * 8.0f;
        const __m128 distance = _mm_set_ps(0.0f, 0.0f, Config::weatherFogDistance, 0.0f);
        const __m128 density = _mm_set_ps(0.0f, 0.0f, falloff, Config::weatherFogDensity);
        const __m128 color = _mm_set_ps(0.0f, Config::weatherFogColor.z,
            Config::weatherFogColor.y, Config::weatherFogColor.x);
        const auto distanceResult = setShaderParam(output, fogDistanceHash,
            reinterpret_cast<__m128i*>(const_cast<__m128*>(&distance)));
        const auto densityResult = setShaderParam(output, fogDensityHash,
            reinterpret_cast<__m128i*>(const_cast<__m128*>(&density)));
        const auto colorResult = setShaderParam(output, fogColorHash,
            reinterpret_cast<__m128i*>(const_cast<__m128*>(&color)));
        using SetShaderParamIntFn = std::uintptr_t (__fastcall*)(__m128i*, std::uint32_t, int);
        const auto enabledResult = reinterpret_cast<SetShaderParamIntFn>(setShaderParamInt)(
            output + 17, fogEnabledHash, 1);
        *mode = 0;
        DebugConsole::once("weather.fog.applied",
            "[weather] fog parameters submitted: density=%.2f anisotropy=%.2f distance=%.0f color=%.2f,%.2f,%.2f results=%llX,%llX,%llX,%llX",
            Config::weatherFogDensity, Config::weatherFogAnisotropy, Config::weatherFogDistance,
            Config::weatherFogColor.x, Config::weatherFogColor.y, Config::weatherFogColor.z,
            static_cast<unsigned long long>(distanceResult),
            static_cast<unsigned long long>(densityResult),
            static_cast<unsigned long long>(colorResult),
            static_cast<unsigned long long>(enabledResult));
        return true;
    }

    static std::uint8_t colorByte(float component)
    {
        return static_cast<std::uint8_t>(std::clamp(component, 0.0f, 255.0f) + 0.5f);
    }

    static C_ByteColor4 tintWorldColor(const C_ByteColor4& original)
    {
        constexpr float redWeight = 0.2126f;
        constexpr float greenWeight = 0.7152f;
        constexpr float blueWeight = 0.0722f;
        const float shade = redWeight * original.r + greenWeight * original.g + blueWeight * original.b;
        return {
            colorByte(shade * Config::NightColor.x),
            colorByte(shade * Config::NightColor.y),
            colorByte(shade * Config::NightColor.z),
            original.a
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

		if (!readMemory(objectData + 0x4u, count) ||
			!readMemory(objectData + 0x38u, startIndex))
			return false;

		if (count <= 0 || count > (1 << 20) || startIndex < 0 || startIndex > (1 << 22))
			return false;

		std::uintptr_t queue = 0;
		const auto queueGlobal = resolveLightDataQueueGlobal();
		if (!queueGlobal || !readMemory(queueGlobal, queue) || !queue ||
			!readMemory(queue + 0x18u, lightBase) || !lightBase)
			return false;

		return true;
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

	int written = 0;
	for (int i = 0; i < count; ++i)
	{
		const auto address = lightBase + (static_cast<std::uintptr_t>(startIndex + i) << 5);
		C_ByteColor4 current{};
		if (!readSceneColor(address, current))
			continue;

		const C_ByteColor4 configured{
			colorByte(Config::NightColor.x * 255.0f),
			colorByte(Config::NightColor.y * 255.0f),
			colorByte(Config::NightColor.z * 255.0f),
			current.a
		};
		if (writeSceneColor(address, configured))
			++written;
	}
	DebugConsole::once("world.night.aggregate_color",
		"[runtime] Night wall tint wrote aggregate records: count=%d written=%d", count, written);
}

static void applyNightExposure(std::uint32_t hash, __m128i*& value)
{
	constexpr std::uint32_t brightnessExposureBias = 0x2858A7F6;
	constexpr std::uint32_t renderOnlyExposureBias = 0x8B3B1F63;
	static __m128 nightExposure{};
	if (Config::Night &&
		(hash == brightnessExposureBias || hash == renderOnlyExposureBias))
	{
		constexpr float darkness = 0.5f;
		DebugConsole::once("world.night.exposure.hit",
			"[runtime] Night exposure parameter reached: hash=0x%08X darkness=%.2f", hash, darkness);
		nightExposure = _mm_set_ps1(-darkness * 4.0f);
		value = reinterpret_cast<__m128i*>(&nightExposure);
	}

}

std::uintptr_t __fastcall H::hkSetShaderParam(__m128i* map, std::uint32_t hash, __m128i* value)
{
	const auto original = H::SetShaderParam.GetOriginal();
	if (!original)
		return 0;

	applyNightExposure(hash, value);
	applyWeatherWind(hash, value);
	return original(map, hash, value);
}

std::uintptr_t __fastcall H::hkSetPostprocessVec(__m128i* map, std::uint32_t hash, __m128i* value)
{
	const auto original = H::SetPostprocessVec.GetOriginal();
	if (!original)
		return 0;

	applyNightExposure(hash, value);
	applyWeatherWind(hash, value);
	return original(map, hash, value);
}

std::uintptr_t __fastcall H::hkSetupFog(__m128i* output, int* mode)
{
	const auto original = H::SetupFog.GetOriginal();
    DebugConsole::once("weather.fog.detour",
        "[weather] SetupFog detour reached; fog enabled=%d output=%p mode=%p",
        Config::weatherFogEnabled ? 1 : 0, output, mode);
    if (Config::weatherFogEnabled)
        DebugConsole::once("weather.fog.active",
            "[weather] fog override is active; applying configured values");
    if (applyWeatherFog(output, mode))
        return 0;
    if (!original)
    {
        DebugConsole::rateLimited("weather.fog.original",
            "[weather] SetupFog original function is unavailable");
        return 0;
    }
    return original(output, mode);
}

void H::releaseRain()
{
	releaseWeatherRain();
}

void H::updateRain()
{
	if (!Config::weatherRainEnabled || !H::oGetLocalPlayer)
	{
        if (Config::weatherRainEnabled)
            DebugConsole::rateLimited("weather.rain.local_fn",
                "[weather] rain enabled but local-player function is unavailable");
		releaseWeatherRain();
		return;
	}

	static std::uintptr_t observedGameRules = 0;
	static float lastRoundStartTime = 0.0f;
	static bool hasRoundStartTime = false;
	const auto client = modules.getModule("client");
	std::uintptr_t gameRules = 0;
	if (!client || !SafeMemory::read(client + cs2_dumper::offsets::client_dll::dwGameRules, gameRules) || !gameRules)
	{
		DebugConsole::rateLimited("weather.rain.no_game_rules",
			"[weather] rain waiting for an active match game-rules object");
		releaseWeatherRain();
		observedGameRules = 0;
		hasRoundStartTime = false;
		return;
	}

	if (observedGameRules != gameRules)
	{
		releaseWeatherRain();
		observedGameRules = gameRules;
		hasRoundStartTime = false;
		DebugConsole::once("weather.rain.match_active",
			"[weather] active match detected; rain will be created for the current game");
	}

	static const std::uint32_t roundStartOffset = SchemaFinder::Get("C_CSGameRules->m_fRoundStartTime");
	float roundStartTime = 0.0f;
	if (roundStartOffset && SafeMemory::read(gameRules + roundStartOffset, roundStartTime))
	{
		if (hasRoundStartTime && roundStartTime != lastRoundStartTime)
			releaseWeatherRain();
		lastRoundStartTime = roundStartTime;
		hasRoundStartTime = true;
	}

	auto* pawn = H::oGetLocalPlayer(0);
	if (!pawn)
	{
		releaseWeatherRain();
		return;
	}

	if (weatherEffectIndex == kInvalidWeatherEffect && !createWeatherRain())
    {
        DebugConsole::rateLimited("weather.rain.create_wait",
            "[weather] rain effect creation is waiting for its manager, resource system, or particle functions");
		return;
    }

	static std::uint32_t sceneNodeOffset = 0;
	static std::uint32_t originOffset = 0;
	if (!sceneNodeOffset)
		sceneNodeOffset = SchemaFinder::Get("C_BaseEntity->m_pGameSceneNode");
	if (!originOffset)
		originOffset = SchemaFinder::Get("CGameSceneNode->m_vecAbsOrigin");
	if (!sceneNodeOffset || !originOffset)
    {
        DebugConsole::rateLimited("weather.rain.schema",
            "[weather] rain waiting for scene schema offsets: node=0x%X origin=0x%X",
            sceneNodeOffset, originOffset);
		return;
    }

	std::uintptr_t sceneNode = 0;
	WeatherVector3 origin{};
	if (!SafeMemory::read(reinterpret_cast<std::uintptr_t>(pawn) + sceneNodeOffset, sceneNode) || !sceneNode ||
		!SafeMemory::read(sceneNode + originOffset, origin))
    {
        DebugConsole::rateLimited("weather.rain.origin",
            "[weather] rain could not read local scene origin: pawn=%p scene=%p", pawn,
            reinterpret_cast<void*>(sceneNode));
		return;
    }

	static const auto setControlPoint = weatherFunction("client",
		"48 83 EC 58 F3 41 0F 10 51 04 F3 41 0F 10 09 F3 41 0F 10 59 08");
	static const auto setTransform = weatherFunction("client",
		"48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 40 48 8B F9 49 8B E9");
	const auto manager = weatherParticleManager();
	if (!manager || !setControlPoint)
    {
        DebugConsole::rateLimited("weather.rain.control_point",
            "[weather] rain effect exists but update functions are unavailable: manager=%p control_point=%p transform=%p",
            reinterpret_cast<void*>(manager), reinterpret_cast<void*>(setControlPoint),
            reinterpret_cast<void*>(setTransform));
		return;
    }

	using SetControlPointFn = bool (__fastcall*)(std::uintptr_t, std::uint32_t, int, const void*, int);
	const auto setPoint = reinterpret_cast<SetControlPointFn>(setControlPoint);
	WeatherParticleTransform transform{ origin.x, origin.y, origin.z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
	if (Config::weatherWindEnabled)
	{
		constexpr float radiansPerDegree = 0.017453292519943295f;
		const float direction = Config::weatherWindDirection * radiansPerDegree;
		const float strength = std::clamp(Config::weatherWindStrength / 5.0f, 0.0f, 1.0f);
		const float turbulence = std::clamp(Config::weatherWindTurbulence / 5.0f, 0.0f, 1.0f);
		float vx = strength * std::cos(direction);
		float vy = strength * std::sin(direction);
		if (turbulence > 0.0f)
		{
			const float time = static_cast<float>(GetTickCount64()) * 0.0006f;
			const float noiseX = 0.60f * std::sin(time * 0.90f + 0.3f) +
				0.30f * std::sin(time * 2.30f + 1.7f) + 0.15f * std::sin(time * 5.10f + 4.2f);
			const float noiseY = 0.60f * std::sin(time * 1.10f + 2.0f) +
				0.30f * std::sin(time * 2.70f + 0.5f) + 0.15f * std::sin(time * 4.60f + 3.1f);
			vx += turbulence * 0.9f * noiseX;
			vy += turbulence * 0.9f * noiseY;
		}

		const float magnitude = (std::min)(std::sqrt(vx * vx + vy * vy), 1.0f);
		const float tilt = magnitude * 80.0f * radiansPerDegree;
		const float heading = (vx != 0.0f || vy != 0.0f) ? std::atan2(vy, vx) : 0.0f;
		const float sine = std::sin(tilt * 0.5f);
		transform.qx = std::sin(heading) * sine;
		transform.qy = -std::cos(heading) * sine;
		transform.qw = std::cos(tilt * 0.5f);
	}

	bool originUpdated = false;
	if (setTransform)
	{
		using SetTransformFn = bool (__fastcall*)(std::uintptr_t, std::uint32_t, int, const void*, int);
		originUpdated = reinterpret_cast<SetTransformFn>(setTransform)(manager,
			static_cast<std::uint32_t>(weatherEffectIndex), 0, &transform, 0);
	}
	else
	{
		originUpdated = setPoint(manager, static_cast<std::uint32_t>(weatherEffectIndex), 0, &origin, 0);
	}

	const float intensity = Config::weatherRainIntensity * Config::weatherRainColor.w;
	const WeatherVector3 color{
		Config::weatherRainColor.x * 255.0f * intensity,
		Config::weatherRainColor.y * 255.0f * intensity,
		Config::weatherRainColor.z * 255.0f * intensity
	};
	const bool colorUpdated = setPoint(manager,
		static_cast<std::uint32_t>(weatherEffectIndex), 1, &color, 0);
	if (!originUpdated || !colorUpdated)
		DebugConsole::rateLimited("weather.rain.update_failed",
			"[weather] particle update rejected: effect=%d origin=%d color=%d",
			weatherEffectIndex, originUpdated ? 1 : 0, colorUpdated ? 1 : 0);
	else
		DebugConsole::once("weather.rain.updated",
			"[weather] rain particle origin and color control points are updating");
}

void __fastcall H::hkUpdateSceneObject(void* a1, void* a2, c_aggregate_object_array* a3)
{
	DebugConsole::once("world.scene_hook.first", "[runtime] UpdateWallsObject/DrawAggregateSceneObjectArray detour reached");
	static auto original = H::UpdateWallsObject.GetOriginal();
	if (!original)
		return;

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

static bool isSkyEntity(C_BaseEntity* entity)
{
    const char* name = entity ? entity->designerName() : nullptr;
    return name && hash_32_fnv1a_const(name) == hash_32_fnv1a_const("env_sky");
}

static bool prepareSkyboxColor(void* meshArray, int meshCount)
{
	if (!Config::Night || !meshArray || meshCount <= 0 || meshCount > (1 << 20))
		return false;

	const auto meshBase = reinterpret_cast<std::uintptr_t>(meshArray);
	std::uintptr_t descriptor = 0;
	if (!readMemory(meshBase + (static_cast<std::uintptr_t>(meshCount) * 0x70u) - 0x58u, descriptor) ||
		descriptor < 0x10000u || descriptor > 0x00007FFFFFFFFFFFull)
		return false;

	activeSkyboxColorAddress = descriptor + 0xE8u;
	if (!readMemory(activeSkyboxColorAddress + 0x0u, activeSkyboxOriginalColor[0]) ||
		!readMemory(activeSkyboxColorAddress + 0x4u, activeSkyboxOriginalColor[1]) ||
		!readMemory(activeSkyboxColorAddress + 0x8u, activeSkyboxOriginalColor[2]))
	{
		activeSkyboxColorAddress = 0;
		return false;
	}

	const float skyBrightness = std::clamp(
		(std::max)((std::max)(activeSkyboxOriginalColor[0], activeSkyboxOriginalColor[1]), activeSkyboxOriginalColor[2]),
		0.0f, 1.0f);
	if (!writeMemory(activeSkyboxColorAddress + 0x0u, skyBrightness * Config::NightSkyColor.x) ||
		!writeMemory(activeSkyboxColorAddress + 0x4u, skyBrightness * Config::NightSkyColor.y) ||
		!writeMemory(activeSkyboxColorAddress + 0x8u, skyBrightness * Config::NightSkyColor.z))
	{
		writeMemory(activeSkyboxColorAddress + 0x0u, activeSkyboxOriginalColor[0]);
		writeMemory(activeSkyboxColorAddress + 0x4u, activeSkyboxOriginalColor[1]);
		writeMemory(activeSkyboxColorAddress + 0x8u, activeSkyboxOriginalColor[2]);
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
	(void)object;
}

void H::applyNightPrimitiveColor(void* batch, int batchCount)
{
	if (!Config::Night || !batch || batchCount <= 0 || batchCount > (1 << 20))
		return;

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
		C_ByteColor4 original{};
		if (readSceneColor(colorAddress, original))
			writeSceneColor(colorAddress, tintWorldColor(original));
	}
}

std::uintptr_t __fastcall H::hkUpdateLightObject(void* a1, void* a2, void* a3)
{
	static auto original = H::UpdateLightObject.GetOriginal();
	if (!original)
		return 0;

	const auto result = original(a1, a2, a3);
	applyNightLightColor(a2);
	return result;
}

std::uintptr_t __fastcall H::hkDrawSceneObject(void* a1, void* a2, void* batch,
	int batchCount, int a5, void* a6, void* a7, void* a8)
{
	static auto original = H::DrawSceneObject.GetOriginal();
	if (!original)
		return 0;

	H::applyNightPrimitiveColor(batch, batchCount);
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

    const SkyColor color{
        colorByte(Config::NightSkyColor.x * 255.0f),
        colorByte(Config::NightSkyColor.y * 255.0f),
        colorByte(Config::NightSkyColor.z * 255.0f),
        colorByte(Config::NightSkyColor.w * 255.0f)
    };
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
