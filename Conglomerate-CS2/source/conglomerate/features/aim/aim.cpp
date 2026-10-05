#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../conglomerate/interfaces/CGameEntitySystem/CGameEntitySystem.h"
#include "../../../conglomerate/interfaces/interfaces.h"
#include "../../../conglomerate/hooks/hooks.h"
#include "../../../conglomerate/config/config.h"
#include "../../../conglomerate/utils/debug_console.h"
#include "../../../conglomerate/utils/memory/patternscan/patternscan.h"

namespace
{
    using GetViewAnglesFn = QAngle_t* (__fastcall*)(void*, int);
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
