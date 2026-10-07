#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../conglomerate/interfaces/CGameEntitySystem/CGameEntitySystem.h"
#include "../../../conglomerate/interfaces/interfaces.h"
#include "../../../conglomerate/hooks/hooks.h"
#include "../../../conglomerate/config/config.h"
#include "../../../conglomerate/utils/debug_console.h"
#include "../../../conglomerate/utils/memory/patternscan/patternscan.h"
#include "../../../conglomerate/utils/memory/safe_memory.h"
#include "../../../conglomerate/utils/memory/seh_diagnostics.h"
#include "../../../conglomerate/offsets/buttons.hpp"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

Vector_t GetEntityEyePos(const C_CSPlayerPawn* Entity);

namespace
{
    using GetViewAnglesFn = QAngle_t* (__fastcall*)(void*, int);
    GetViewAnglesFn resolveGetViewAngles();

    struct TraceFilter
    {
        std::uintptr_t vtable{};
        std::uintptr_t mask{};
        std::int64_t values[2]{};
        int skipHandles[4]{};
        std::int16_t collisions[2]{};
        std::int16_t value2{};
        std::uint8_t layer{};
        std::uint8_t flags{};
        std::uint8_t value5{};
        std::uint8_t value6{};
        std::byte padding[6]{};
        char value7{};
    };

    struct TraceRay
    {
        Vector_t mins{};
        Vector_t maxs{};
        std::byte padding[0x10]{};
        std::uint8_t type{};
        std::byte tail[7]{};
    };

    struct TraceResult
    {
        void* surface{};
        std::uintptr_t hitEntity{};
        void* hitboxData{};
        std::byte padding0[0x38]{};
        std::uint32_t contents{};
        std::byte padding1[0x24]{};
        Vector_t startPosition{};
        Vector_t endPosition{};
        Vector_t normal{};
        Vector_t position{};
        std::byte padding2[4]{};
        float fraction{};
        std::byte padding3[6]{};
        bool allSolid{};
        std::byte padding4[0x4d]{};
    };

    using TraceFilterInitFn = void(__fastcall*)(TraceFilter*, std::uintptr_t, std::uintptr_t, std::uint8_t, int);
    using TraceRayFn = bool(__fastcall*)(std::uintptr_t, TraceRay*, const Vector_t*, const Vector_t*, TraceFilter*, TraceResult*);

    static_assert(sizeof(TraceFilter) == 0x48);
    static_assert(sizeof(TraceResult) == 0x108);

    bool ResolveTraceFunctions(std::uintptr_t& manager, TraceFilterInitFn& initializeFilter, TraceRayFn& traceRay)
    {
        static const auto managerPattern = M::FindPattern("client", "48 8B 0D ? ? ? ? 48 8D 34 52");
        static const auto filterPattern = M::FindPattern("client",
            "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F B6 41 ? 33 FF 24");
        static const auto tracePattern = M::FindPattern("client",
            "48 89 54 24 ? 48 89 4C 24 ? 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? B8");
        static const auto managerGlobal = reinterpret_cast<std::uintptr_t>(M::abs(managerPattern, 3));

        if (!managerGlobal || !filterPattern || !tracePattern ||
            !SafeMemory::read(managerGlobal, manager) || !manager)
            return false;

        initializeFilter = reinterpret_cast<TraceFilterInitFn>(filterPattern);
        traceRay = reinterpret_cast<TraceRayFn>(tracePattern);
        return true;
    }

    bool IsFirearm(C_CSPlayerPawn* pawn)
    {
        if (!pawn)
            return false;

        __try
        {
            auto* weapon = pawn->GetActiveWeapon();
            if (!weapon)
            {
                DebugConsole::rateLimited("trigger.weapon.missing",
                    "[runtime] Trigger waiting: local pawn has no active weapon");
                return false;
            }

            const char* designerName = reinterpret_cast<CEntityInstance*>(weapon)->designerName();
            auto* data = H::oGetWeaponData ? weapon->Data() : nullptr;
            const char* dataName = data ? data->m_szName() : nullptr;
            const char* name = (dataName && std::strncmp(dataName, "weapon_", 7) == 0)
                ? dataName : designerName;
            if (!name || std::strncmp(name, "weapon_", 7) != 0)
            {
                DebugConsole::rateLimited("trigger.weapon.classify",
                    "[runtime] Trigger item rejected: designer=%s data=%s",
                    designerName ? designerName : "<null>", dataName ? dataName : "<null>");
                return false;
            }

            constexpr const char* nonFirearms[] = {
                "knife", "bayonet", "taser", "grenade", "molotov", "incgrenade",
                "c4", "healthshot", "breachcharge", "bumpmine", "snowball",
                "melee", "tablet", "fists", "zone_repulsor"
            };
            for (const char* excluded : nonFirearms)
            {
                if (std::strstr(name, excluded))
                {
                    DebugConsole::rateLimited("trigger.weapon.nonfirearm",
                        "[runtime] Trigger item rejected as non-firearm: %s", name);
                    return false;
                }
            }
            DebugConsole::once("trigger.weapon.accepted", "[runtime] Trigger accepted firearm: %s", name);
            return true;
        }
        __except (SehDiagnostics::handle("trigger.weapon_data"))
        {
            return false;
        }
    }

    bool TraceDirection(C_CSPlayerPawn* localPawn, const Vector_t& start, const Vector_t& direction,
        std::uintptr_t& hitEntity)
    {
        if (!localPawn || !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z))
            return false;
        constexpr float traceDistance = 8192.0f;
        const Vector_t end(start.x + direction.x * traceDistance,
            start.y + direction.y * traceDistance,
            start.z + direction.z * traceDistance);

        std::uintptr_t traceManager = 0;
        TraceFilterInitFn initializeFilter = nullptr;
        TraceRayFn traceRay = nullptr;
        if (!ResolveTraceFunctions(traceManager, initializeFilter, traceRay))
            return false;

        TraceFilter filter{};
        initializeFilter(&filter, reinterpret_cast<std::uintptr_t>(localPawn), 0x1c3003u, 4, 7);
        TraceRay ray{};
        TraceResult result{};
        result.fraction = 1.0f;
        if (!traceRay(traceManager, &ray, &start, &end, &filter, &result))
            return false;

        hitEntity = result.hitEntity;
        return hitEntity != 0;
    }

    bool TraceCrosshair(void* input, int slot, C_CSPlayerPawn* localPawn,
        Vector_t& start, Vector_t& forward, Vector_t& right, Vector_t& up, std::uintptr_t& hitEntity)
    {
        const auto getViewAngles = resolveGetViewAngles();
        void* activeInput = I::Input ? I::Input : input;
        if (!activeInput || !getViewAngles || !localPawn)
            return false;

        QAngle_t* angles = getViewAngles(activeInput, slot);
        if (!angles || !std::isfinite(angles->x) || !std::isfinite(angles->y))
            return false;

        start = GetEntityEyePos(localPawn);
        if (start.x == 0.0f && start.y == 0.0f && start.z == 0.0f)
            return false;

        constexpr float degreesToRadians = 3.14159265358979323846f / 180.0f;
        const float pitch = angles->x * degreesToRadians;
        const float yaw = angles->y * degreesToRadians;
        forward = Vector_t(std::cos(pitch) * std::cos(yaw),
            std::cos(pitch) * std::sin(yaw), -std::sin(pitch));
        right = Vector_t(-std::sin(yaw), std::cos(yaw), 0.0f);
        up = Vector_t(std::sin(pitch) * std::cos(yaw), std::sin(pitch) * std::sin(yaw), std::cos(pitch));
        return TraceDirection(localPawn, start, forward, hitEntity);
    }

    bool TriggerHitchance(C_CSPlayerPawn* localPawn, const Vector_t& start,
        const Vector_t& forward, const Vector_t& right, const Vector_t& up,
        std::uintptr_t targetEntity, int minimumPercent, int& hitPercent)
    {
        hitPercent = 0;
        if (minimumPercent <= 0)
            return true;

        auto* weapon = localPawn ? localPawn->GetActiveWeapon() : nullptr;
        if (!weapon)
            return false;

        using GetSpreadFn = float(__fastcall*)(std::uintptr_t);
        using GetInaccuracyFn = float(__fastcall*)(std::uintptr_t, float*, float*);
        using UpdateAccuracyFn = void(__fastcall*)(std::uintptr_t);
        using CalculateSpreadFn = void(__fastcall*)(std::int16_t, int, int, std::uint32_t,
            float, float, float, float*, float*);
        static const auto getSpread = reinterpret_cast<GetSpreadFn>(M::FindPattern("client",
            "48 63 91 00 1A 00 00 48 8B 81 88 03 00 00 85 D2 78 ? 48 83 FA 02 73 ? F3 0F 10 84 90 50 07 00 00"));
        static const auto getInaccuracy = reinterpret_cast<GetInaccuracyFn>(M::FindPattern("client",
            "48 89 5C 24 ? 55 56 57 48 81 EC ? ? ? ? 44 0F 29 84 24"));
        static const auto updateAccuracy = reinterpret_cast<UpdateAccuracyFn>(M::FindPattern("client",
            "40 57 41 56 48 83 EC 68 48 8B F9 E8 ? ? ? ? 4C 8B F0 48 85 C0"));
        static const auto calculateSpreadCall = M::FindPattern("client",
            "28 F3 44 0F 11 44 24 20 E8 ? ? ? ? 48 8D 85 B0 00 00 00");
        static const auto calculateSpread = calculateSpreadCall
            ? reinterpret_cast<CalculateSpreadFn>(M::abs(calculateSpreadCall + 8, 1)) : nullptr;
        if (!getSpread || !getInaccuracy || !updateAccuracy || !calculateSpread)
        {
            DebugConsole::rateLimited("trigger.hitchance.patterns",
                "[runtime] Trigger hitchance unavailable: Weapon accuracy/spread functions were not found");
            return false;
        }

        const auto weaponAddress = reinterpret_cast<std::uintptr_t>(weapon);
        const float spread = getSpread(weaponAddress);
        const auto accuracyBegin = SchemaFinder::Get("C_CSWeaponBase->m_flTurningInaccuracyDelta");
        const auto accuracyEnd = SchemaFinder::Get("C_CSWeaponBase->m_flRecoilIndex");
        if (accuracyBegin == 0U || accuracyEnd < accuracyBegin)
            return false;
        const std::size_t accuracyStateSize = static_cast<std::size_t>(accuracyEnd - accuracyBegin) + sizeof(float);
        if (accuracyStateSize == 0U || accuracyStateSize > 0x100U)
            return false;

        std::array<std::byte, 0x100> accuracyBackup{};
        auto* accuracyState = reinterpret_cast<void*>(weaponAddress + accuracyBegin);
        std::memcpy(accuracyBackup.data(), accuracyState, accuracyStateSize);
        updateAccuracy(weaponAddress);
        const float inaccuracy = getInaccuracy(weaponAddress, nullptr, nullptr);
        std::memcpy(accuracyState, accuracyBackup.data(), accuracyStateSize);

        std::uint16_t itemDefinition = 0;
        const auto attributeManagerOffset = SchemaFinder::Get("C_EconEntity->m_AttributeManager");
        const auto itemOffset = SchemaFinder::Get("C_AttributeContainer->m_Item");
        const auto itemDefinitionOffset = SchemaFinder::Get("C_EconItemView->m_iItemDefinitionIndex");
        if (attributeManagerOffset == 0U || itemOffset == 0U || itemDefinitionOffset == 0U)
            return false;
        const auto itemDefinitionAddress = weaponAddress + attributeManagerOffset + itemOffset + itemDefinitionOffset;
        if (!SafeMemory::read(itemDefinitionAddress, itemDefinition) || itemDefinition == 0)
            return false;

        const auto subclassOffset = SchemaFinder::Get("C_BaseEntity->m_nSubclassID");
        const auto bulletCountOffset = SchemaFinder::Get("CCSWeaponBaseVData->m_nNumBullets");
        std::uintptr_t weaponVData = 0;
        int bulletCount = 1;
        if (subclassOffset != 0U && bulletCountOffset != 0U &&
            SafeMemory::read(weaponAddress + subclassOffset + 0x8U, weaponVData) && weaponVData)
        {
            SafeMemory::read(weaponVData + bulletCountOffset, bulletCount);
        }

        const auto recoilOffset = SchemaFinder::Get("C_CSWeaponBase->m_flRecoilIndex");
        float recoilIndex = 0.0f;
        if (recoilOffset == 0U || !SafeMemory::read(weaponAddress + recoilOffset, recoilIndex))
            return false;

        if (!std::isfinite(spread) || !std::isfinite(inaccuracy) || spread < 0.0f ||
            inaccuracy < 0.0f || !std::isfinite(recoilIndex) || bulletCount <= 0 || bulletCount > 32)
            return false;

        constexpr int sampleCount = 128;
        const int requiredHits = (minimumPercent * sampleCount + 99) / 100;
        int hits = 0;
        for (int sample = 0; sample < sampleCount; ++sample)
        {
            float spreadX = 0.0f;
            float spreadY = 0.0f;
            calculateSpread(static_cast<std::int16_t>(itemDefinition), bulletCount, 0,
                static_cast<std::uint32_t>(sample + 1), inaccuracy, spread,
                recoilIndex, &spreadX, &spreadY);
            if (!std::isfinite(spreadX) || !std::isfinite(spreadY))
                continue;

            Vector_t direction(
                forward.x + right.x * spreadX + up.x * spreadY,
                forward.y + right.y * spreadX + up.y * spreadY,
                forward.z + right.z * spreadX + up.z * spreadY);
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
            if (length <= 0.0f || !std::isfinite(length))
                continue;
            direction.x /= length;
            direction.y /= length;
            direction.z /= length;

            std::uintptr_t sampleHit = 0;
            // Count only rays that still intersect the entity hit by the center ray.
            if (TraceDirection(localPawn, start, direction, sampleHit) && sampleHit == targetEntity)
                ++hits;

        }
        hitPercent = hits * 100 / sampleCount;
        return hits >= requiredHits;
    }

    bool g_triggerAttackPressed = false;
    ULONGLONG g_lastTriggerAttack = 0;
    ULONGLONG g_triggerLowHitchanceStart = 0;
    std::uintptr_t g_triggerPendingEntity = 0;

    bool WriteTriggerAttack(std::int32_t state)
    {
        const auto clientBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("client.dll"));
        return clientBase && SafeMemory::write(clientBase + cs2_dumper::buttons::attack, state);
    }

    using SetViewAnglesFn = void (__fastcall*)(void*, int, QAngle_t*);

    GetViewAnglesFn resolveGetViewAngles()
    {
        static auto fn = reinterpret_cast<GetViewAnglesFn>(M::FindPattern(
            "client", "4C 8B C1 85 D2 74 ? 48 8D 05"));
        return fn;
    }

    SetViewAnglesFn resolveSetViewAngles()
    {
        static auto fn = reinterpret_cast<SetViewAnglesFn>(M::FindPattern(
            "client", "85 D2 75 ? 48 63 81"));
        return fn;
    }
}


Vector_t GetEntityEyePos(const C_CSPlayerPawn* Entity) {
    if (!Entity)
        return {};

    const std::uint32_t sceneNodeOffset = SchemaFinder::Get("C_BaseEntity->m_pGameSceneNode");
    const std::uint32_t originOffset = SchemaFinder::Get("CGameSceneNode->m_vecAbsOrigin");
    const std::uint32_t viewOffsetOffset = SchemaFinder::Get("C_BaseModelEntity->m_vecViewOffset");
    if (sceneNodeOffset == 0U || originOffset == 0U || viewOffsetOffset == 0U)
        return {};

    uintptr_t game_scene_node = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(Entity) + sceneNodeOffset);
    if (!game_scene_node)
        return {};

    auto Origin = *reinterpret_cast<Vector_t*>(game_scene_node + originOffset);
    auto ViewOffset = *reinterpret_cast<Vector_t*>(reinterpret_cast<uintptr_t>(Entity) + viewOffsetOffset);

    Vector_t Result = Origin + ViewOffset;
    if (!std::isfinite(Result.x) || !std::isfinite(Result.y) || !std::isfinite(Result.z))
        return {};

    return Result;
}

inline QAngle_t CalcAngles(Vector_t viewPos, Vector_t aimPos)
{
    QAngle_t angle = { 0, 0, 0 };

    Vector_t delta = aimPos - viewPos;

    angle.x = -asin(delta.z / delta.Length()) * (180.0f / 3.141592654f);
    angle.y = atan2(delta.y, delta.x) * (180.0f / 3.141592654f);

    return angle;
}

inline float GetFov(const QAngle_t& viewAngle, const QAngle_t& aimAngle)
{
    QAngle_t delta = (aimAngle - viewAngle).Normalize();

    return sqrtf(powf(delta.x, 2.0f) + powf(delta.y, 2.0f));
}

void ReleaseTriggerBot()
{
    if (g_triggerAttackPressed)
        WriteTriggerAttack(256);
    g_triggerAttackPressed = false;
    g_triggerLowHitchanceStart = 0;
    g_triggerPendingEntity = 0;
}

void TriggerBot(void* input, int slot, bool allowed)
{
    if (!allowed || !Config::triggerbot || !H::oGetLocalPlayer || !I::GameEntity || !I::GameEntity->Instance)
    {
        if (Config::triggerbot && !allowed)
            DebugConsole::once("trigger.blocked", "[runtime] Trigger skipped: input is blocked by the menu");
        ReleaseTriggerBot();
        return;
    }

    C_CSPlayerPawn* localPawn = H::oGetLocalPlayer(0);
    if (!localPawn)
    {
        DebugConsole::rateLimited("trigger.local.null", "[runtime] Trigger skipped: GetLocalPawn returned null");
        ReleaseTriggerBot();
        return;
    }

    const int localHealth = localPawn->getHealth();
    if (localHealth <= 0)
    {
        DebugConsole::rateLimited("trigger.local.dead",
            "[runtime] Trigger skipped: local pawn is not alive (health=%d)", localHealth);
        ReleaseTriggerBot();
        return;
    }
    if (!IsFirearm(localPawn))
    {
        ReleaseTriggerBot();
        return;
    }

    std::uintptr_t hitEntity = 0;
    Vector_t traceStart{}, forward{}, right{}, up{};
    if (!TraceCrosshair(input, slot, localPawn, traceStart, forward, right, up, hitEntity))
    {
        DebugConsole::rateLimited("trigger.trace.miss",
            "[runtime] Trigger trace did not hit an entity (view/trace setup or crosshair miss)");
        ReleaseTriggerBot();
        return;
    }

    bool enemyHit = false;
    for (int i = 1; i <= 64; ++i)
    {
        auto* entity = I::GameEntity->Instance->Get(i);
        if (!entity || !entity->IsPlayerController() || !entity->handle().valid())
            continue;

        auto* controller = reinterpret_cast<CCSPlayerController*>(entity);
        if (controller->IsLocalPlayer() || !controller->m_hPlayerPawn().valid())
            continue;

        auto* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(controller->m_hPlayerPawn().index());
        if (!pawn || reinterpret_cast<std::uintptr_t>(pawn) != hitEntity || pawn->getHealth() <= 0)
            continue;
        if (Config::team_check && pawn->getTeam() == localPawn->getTeam())
            continue;

        enemyHit = true;
        break;
    }

    if (!enemyHit)
    {
        DebugConsole::rateLimited("trigger.trace.not_enemy",
            "[runtime] Trigger trace hit %p but it did not match a live enemy pawn", reinterpret_cast<void*>(hitEntity));
        ReleaseTriggerBot();
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (hitEntity != g_triggerPendingEntity)
    {
        g_triggerPendingEntity = hitEntity;
        g_triggerLowHitchanceStart = now;
        DebugConsole::rateLimited("trigger.target",
            "[runtime] Trigger acquired enemy %p; attack cooldown is %d ms from the last shot",
            reinterpret_cast<void*>(hitEntity), std::clamp(Config::trigger_delay, 0, 250));
    }

    const ULONGLONG delay = static_cast<ULONGLONG>(std::clamp(Config::trigger_delay, 0, 250));
    if (g_triggerAttackPressed)
    {
        WriteTriggerAttack(256);
        g_triggerAttackPressed = false;
        return;
    }

    if (g_lastTriggerAttack != 0 && now - g_lastTriggerAttack < delay)
        return;

    int hitchance = 0;
    const int hitchanceThreshold = std::clamp(Config::trigger_hitchance, 0, 100);
    if (!TriggerHitchance(localPawn, traceStart, forward, right, up,
        hitEntity, hitchanceThreshold, hitchance))
    {
        constexpr ULONGLONG lowHitchanceGraceMs = 5;
        if (g_triggerLowHitchanceStart == 0)
            g_triggerLowHitchanceStart = now;

        if (now - g_triggerLowHitchanceStart < lowHitchanceGraceMs)
        {
            DebugConsole::rateLimited("trigger.hitchance.low",
                "[runtime] Trigger waiting: hitchance=%d%% threshold=%d%%", hitchance, hitchanceThreshold);
            return;
        }

        DebugConsole::rateLimited("trigger.hitchance.fallback",
            "[runtime] Trigger grace elapsed; firing despite hitchance=%d%% threshold=%d%%",
            hitchance, hitchanceThreshold);
    }
    else
        g_triggerLowHitchanceStart = 0;

    if (WriteTriggerAttack(65537))
    {
        g_triggerAttackPressed = true;
        g_lastTriggerAttack = now;
        g_triggerLowHitchanceStart = now;
        DebugConsole::rateLimited("trigger.attack.press",
            "[runtime] Trigger pressed attack; hitchance=%d%% cooldown=%llu ms",
            hitchance, static_cast<unsigned long long>(delay));
    }
}

void Aimbot(void* input, int slot) {
    if (!Config::aimbot)
        return;

    if (!H::oGetLocalPlayer)
	{
		DebugConsole::rateLimited("aim.local_fn.missing", "[runtime] aim skipped: GetLocalPawn function pointer is null");
        return;
	}

    if (!I::GameEntity || !I::GameEntity->Instance)
	{
		DebugConsole::rateLimited("aim.entity.invalid", "[runtime] aim skipped: entity system unavailable");
        return;
	}

    constexpr int nMaxHighestEntity = 64;

    C_CSPlayerPawn* lp = H::oGetLocalPlayer(0);
    if (!lp)
	{
		DebugConsole::rateLimited("aim.local.null", "[runtime] aim skipped: GetLocalPawn returned null");
        return;
	}

    Vector_t lep = GetEntityEyePos(lp);

    void* activeInput = I::Input ? I::Input : input;
    const auto getViewAngles = resolveGetViewAngles();
    const auto setViewAngles = resolveSetViewAngles();
    if (!activeInput || !getViewAngles || !setViewAngles)
    {
        DebugConsole::rateLimited("aim.viewangles.functions_missing",
            "[runtime] aim skipped: input or view-angle accessor/setter is unavailable");
        return;
    }

    QAngle_t* viewangles = getViewAngles(activeInput, slot);
    if (!viewangles)
    {
        DebugConsole::rateLimited("aim.viewangles.invalid", "[runtime] aim skipped: GetViewAngles returned null");
        return;
    }

    C_CSPlayerPawn* best_pawn = nullptr;
    float best_fov = Config::aimbot_fov;
    QAngle_t best_angle{};
    int controllers = 0;
    int validPawnHandles = 0;
    int resolvedPawns = 0;
    int alivePawns = 0;

    if (Config::aimbot)
    for (int i = 1; i <= nMaxHighestEntity; i++) {
        auto Entity = I::GameEntity->Instance->Get(i);
        if (!Entity)
            continue;

        if (!Entity->handle().valid())
            continue;

        if (!Entity->IsPlayerController())
            continue;
        ++controllers;

        CCSPlayerController* Controller = reinterpret_cast<CCSPlayerController*>(Entity);
        if (!Controller->m_hPlayerPawn().valid())
            continue;
        ++validPawnHandles;
        if (Controller->IsLocalPlayer())
            continue;

        C_CSPlayerPawn* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(Controller->m_hPlayerPawn().index());
        if (!pawn)
            continue;
        ++resolvedPawns;

        if (pawn->getHealth() < 1)
            continue;
        ++alivePawns;

        if (Config::team_check && pawn->getTeam() == lp->getTeam())
            continue;

        Vector_t eye_pos = GetEntityEyePos(pawn);
        QAngle_t angle = CalcAngles(lep, eye_pos);

        const float fov = GetFov(*viewangles, angle);
        if (!std::isfinite(fov) || fov > best_fov)
            continue;

        best_fov = fov;
        best_pawn = pawn;
        best_angle = angle;
    }

    if (best_pawn) {
        DebugConsole::once("aim.target.found", "[runtime] aim found a target; FOV=%.2f", best_fov);
        best_angle.z = 0.f;
        best_angle = best_angle.Normalize();
        setViewAngles(activeInput, slot, &best_angle);
        const QAngle_t* applied = getViewAngles(activeInput, slot);
        DebugConsole::rateLimited("aim.angle.applied",
            "[runtime] aim set view angle: requested=(%.2f, %.2f) current=(%.2f, %.2f) target=%p",
            best_angle.x, best_angle.y,
            applied ? applied->x : 0.0f, applied ? applied->y : 0.0f, best_pawn);
    }
	else
	{
		DebugConsole::rateLimited("aim.target.none",
			"[runtime] aim found no target: fov_limit=%.2f controllers=%d valid_handles=%d resolved_pawns=%d alive=%d",
			best_fov, controllers, validPawnHandles, resolvedPawns, alivePawns);
	}
}
