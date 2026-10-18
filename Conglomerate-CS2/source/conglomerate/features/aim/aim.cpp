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
#include <cmath>
#include <cstddef>
#include <cstdint>

Vector_t GetEntityEyePos(const C_CSPlayerPawn* Entity);

namespace
{
    using GetViewAnglesFn = QAngle_t* (__fastcall*)(void*, int);
    GetViewAnglesFn resolveGetViewAngles();
    using GetAimPunchFn = std::uintptr_t (__fastcall*)(void*, QAngle_t*, int);

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
        static const auto managerPattern = []() -> std::uint8_t*
        {
            auto* found = M::FindPattern("client", "48 8B 0D ? ? ? ? 48 8D 34 52");
            if (!found)
                found = M::FindPattern("client",
                    "48 8B 0D ? ? ? ? 48 8B D0 C7 44 24 ? 04 00 00 00 48 C7 44 24 ? 01 30 1C 00");
            return found;
        }();
        static const auto filterPattern = M::FindPattern("client",
            "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F B6 41 ? 33 FF 24");
        static const auto tracePattern = M::FindPattern("client",
            "48 89 54 24 ? 48 89 4C 24 ? 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? B8");
        static const auto managerGlobal = reinterpret_cast<std::uintptr_t>(M::abs(managerPattern, 3));

        DebugConsole::once("trigger.trace.resolve",
            "[scan] Trigger trace: manager_sig=%p filter_sig=%p trace_sig=%p manager_global=%p",
            static_cast<void*>(managerPattern), static_cast<void*>(filterPattern),
            static_cast<void*>(tracePattern), reinterpret_cast<void*>(managerGlobal));

        if (!managerGlobal || !filterPattern || !tracePattern ||
            !SafeMemory::read(managerGlobal, manager) || !manager)
            return false;

        initializeFilter = reinterpret_cast<TraceFilterInitFn>(filterPattern);
        traceRay = reinterpret_cast<TraceRayFn>(tracePattern);
        return true;
    }

    bool IsValidAddress(std::uintptr_t address)
    {
        return address > 0x10000 && address < 0x7FFFFFFFFFFF;
    }

    bool GetAimBonePosition(C_CSPlayerPawn* pawn, int bone, Vector_t& position)
    {
        if (!pawn || bone < 0 || bone >= 64)
            return false;

        std::uintptr_t scene = 0;
        __try
        {
            scene = reinterpret_cast<std::uintptr_t>(pawn->m_pGameSceneNode());
        }
        __except (SehDiagnostics::handle("aim.bone.scene_node"))
        {
            return false;
        }

        if (!IsValidAddress(scene))
            return false;

        std::uintptr_t boneArray = 0;
        if (!SafeMemory::read(scene + 0x1C0u, boneArray) || !IsValidAddress(boneArray) ||
            !SafeMemory::read(boneArray + static_cast<std::uintptr_t>(bone) * 0x20u, position))
        {
            return false;
        }

        return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
    }

    bool TraceToPoint(C_CSPlayerPawn* localPawn, const Vector_t& start, const Vector_t& end,
        std::uintptr_t& hitEntity)
    {
        if (!localPawn || !std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(start.z) ||
            !std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z))
        {
            return false;
        }

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
        return TraceToPoint(localPawn, start, end, hitEntity) && hitEntity != 0;
    }

    bool TraceCrosshair(void* input, int slot, C_CSPlayerPawn* localPawn,
        Vector_t& start, Vector_t& forward, std::uintptr_t& hitEntity)
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
        return TraceDirection(localPawn, start, forward, hitEntity);
    }

    bool g_triggerAttackPressed = false;

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

    GetAimPunchFn resolveGetAimPunch()
    {
        static auto fn = reinterpret_cast<GetAimPunchFn>(M::FindPattern(
            "client", "48 89 5C 24 08 55 56 57 48 8D 6C 24 B9 48 81 EC"));
        return fn;
    }

    bool getCurrentAimPunch(C_CSPlayerPawn* pawn, QAngle_t& punch, int& shotsFired)
    {
        static std::uint32_t servicesOffset = 0;
        static std::uint32_t shotsFiredOffset = 0;
        if (!servicesOffset)
            servicesOffset = SchemaFinder::Get("C_CSPlayerPawn->m_pAimPunchServices");
        if (!shotsFiredOffset)
            shotsFiredOffset = SchemaFinder::Get("C_CSPlayerPawn->m_iShotsFired");
        if (!pawn || !servicesOffset || !shotsFiredOffset)
        {
            DebugConsole::rateLimited("aim.rcs.schema_missing",
                "[runtime] recoil correction skipped: aim-punch service or shot-count schema field is unavailable");
            return false;
        }

        const auto pawnAddress = reinterpret_cast<std::uintptr_t>(pawn);
        std::uintptr_t services = 0;
        if (!SafeMemory::read(pawnAddress + servicesOffset, services) || !services ||
            !SafeMemory::read(pawnAddress + shotsFiredOffset, shotsFired))
            return false;

        const auto getAimPunch = resolveGetAimPunch();
        if (!getAimPunch)
        {
            DebugConsole::rateLimited("aim.rcs.getter_missing",
                "[runtime] recoil correction skipped: aim-punch service getter signature was not found");
            return false;
        }

        getAimPunch(reinterpret_cast<void*>(services), &punch, 0);
        const bool valid = std::isfinite(punch.x) && std::isfinite(punch.y) &&
            std::abs(punch.x) <= 360.0f && std::abs(punch.y) <= 360.0f;
        if (!valid)
            DebugConsole::rateLimited("aim.rcs.result_invalid", "[runtime] recoil correction skipped: aim-punch getter returned invalid angles");
        return valid;
    }
}

void ApplyRecoilControl(void* input, int slot)
{
    struct RecoilState
    {
        std::uintptr_t pawn{};
        int shotsFired{};
        QAngle_t previousPunch{};
        QAngle_t pendingCorrection{};
        bool initialized{};
    };
    static RecoilState state{};

    if (!Config::recoilControl)
    {
        state = {};
        return;
    }

    if (!H::oGetLocalPlayer)
    {
        state = {};
        return;
    }

    auto* pawn = H::oGetLocalPlayer(0);
    if (!pawn || pawn->getHealth() <= 0)
    {
        state = {};
        return;
    }

    QAngle_t punch{};
    int shotsFired = 0;
    if (!getCurrentAimPunch(pawn, punch, shotsFired))
        return;

    const auto pawnAddress = reinterpret_cast<std::uintptr_t>(pawn);
    if (!state.initialized || state.pawn != pawnAddress || shotsFired < state.shotsFired)
    {
        state.pawn = pawnAddress;
        state.shotsFired = shotsFired;
        state.previousPunch = punch;
        state.pendingCorrection = {};
        state.initialized = true;
        return;
    }

    QAngle_t delta{
        punch.x - state.previousPunch.x,
        punch.y - state.previousPunch.y,
        0.0f
    };
    state.previousPunch = punch;
    state.shotsFired = shotsFired;

    if (shotsFired <= 1)
    {
        state.pendingCorrection = {};
        return;
    }

    const float strength = std::clamp(Config::recoilControlStrength, 0.0f, 100.0f) * 0.01f;
    state.pendingCorrection.x += delta.x * strength;
    state.pendingCorrection.y += delta.y * strength;
    const float smoothing = std::clamp(Config::recoilControlSmoothing, 1.0f, 10.0f);
    const QAngle_t correction{
        state.pendingCorrection.x / smoothing,
        state.pendingCorrection.y / smoothing,
        0.0f
    };
    if (std::abs(correction.x) < 0.0001f && std::abs(correction.y) < 0.0001f)
        return;

    void* activeInput = I::Input ? I::Input : input;
    const auto getViewAngles = resolveGetViewAngles();
    const auto setViewAngles = resolveSetViewAngles();
    if (!activeInput || !getViewAngles || !setViewAngles)
        return;

    QAngle_t* viewAngles = getViewAngles(activeInput, slot);
    if (!viewAngles)
        return;

    QAngle_t corrected = *viewAngles;
    corrected.x -= correction.x;
    corrected.y -= correction.y;
    corrected.z = 0.0f;
    corrected = corrected.Normalize();
    setViewAngles(activeInput, slot, &corrected);
    state.pendingCorrection.x -= correction.x;
    state.pendingCorrection.y -= correction.y;
    DebugConsole::rateLimited("aim.rcs.standalone",
        "[runtime] standalone recoil correction applied: shots=%d pitch=%.3f yaw=%.3f strength=%.0f%% smoothing=%.1f",
        shotsFired, correction.x, correction.y, strength * 100.0f, smoothing);
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
    Vector_t traceStart{}, forward{};
    if (!TraceCrosshair(input, slot, localPawn, traceStart, forward, hitEntity))
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
        if (Config::triggerbot_team_check && pawn->getTeam() == localPawn->getTeam())
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

    if (g_triggerAttackPressed)
    {
        WriteTriggerAttack(256);
        g_triggerAttackPressed = false;
        return;
    }

    if (WriteTriggerAttack(65537))
    {
        g_triggerAttackPressed = true;
        DebugConsole::rateLimited("trigger.attack.press", "[runtime] Trigger pressed attack");
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

        Vector_t aimPoints[3]{};
        int aimPointCount = 0;
        Vector_t point{};
        if (GetAimBonePosition(pawn, 7, point))
            aimPoints[aimPointCount++] = point;
        if (GetAimBonePosition(pawn, 4, point))
            aimPoints[aimPointCount++] = point;
        if (aimPointCount == 0)
        {
            point = GetEntityEyePos(pawn);
            if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
                (point.x != 0.0f || point.y != 0.0f || point.z != 0.0f))
            {
                aimPoints[aimPointCount++] = point;
            }
        }

        for (int pointIndex = 0; pointIndex < aimPointCount; ++pointIndex)
        {
            const QAngle_t angle = CalcAngles(lep, aimPoints[pointIndex]);
            const float fov = GetFov(*viewangles, angle);
            if (!std::isfinite(fov) || fov > best_fov)
                continue;

            if (Config::aimbotVisibleCheck)
            {
                std::uintptr_t hitEntity = 0;
                if (!TraceToPoint(lp, lep, aimPoints[pointIndex], hitEntity) ||
                    hitEntity != reinterpret_cast<std::uintptr_t>(pawn))
                {
                    continue;
                }
            }

            best_fov = fov;
            best_pawn = pawn;
            best_angle = angle;
        }
    }

    if (best_pawn) {
        DebugConsole::once("aim.target.found", "[runtime] aim found a target; FOV=%.2f", best_fov);
        if (Config::recoilControl)
        {
            QAngle_t punch{};
            int shotsFired = 0;
            if (getCurrentAimPunch(lp, punch, shotsFired) && shotsFired > 1)
            {
                const float strength = std::clamp(Config::recoilControlStrength, 0.0f, 100.0f) * 0.01f;
                best_angle.x -= punch.x * strength;
                best_angle.y -= punch.y * strength;
            }
        }
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
