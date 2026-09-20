#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../conglomerate/interfaces/CGameEntitySystem/CGameEntitySystem.h"
#include "../../../conglomerate/interfaces/interfaces.h"
#include "../../../conglomerate/hooks/hooks.h"
#include "../../../conglomerate/config/config.h"

#include <chrono>
#include <Windows.h>

// Literally the most autistic code ive ever written in my life
// Please dont ever make me do this again

static bool is_writable_address(const void* address)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(address, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;

    if (mbi.State != MEM_COMMIT)
        return false;

    switch (mbi.Protect & 0xFFu)
    {
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

Vector_t GetEntityEyePos(const C_CSPlayerPawn* Entity) {
    if (!Entity)
        return {};

    // SchemaFinder::Get() returns 0 for an unresolved field. Adding that 0 to
    // the entity pointer silently turned every failed lookup into a read at
    // the object's vtable pointer.
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

void Aimbot() {
    if (!Config::aimbot)
        return;

    if (!H::oGetLocalPlayer)
        return;

    if (!I::GameEntity || !I::GameEntity->Instance)
        return;

    int nMaxHighestEntity = I::GameEntity->Instance->GetHighestEntityIndex();

    C_CSPlayerPawn* lp = H::oGetLocalPlayer(0);
    if (!lp)
        return;

    Vector_t lep = GetEntityEyePos(lp);

    // Hard-coded RVA. If the module handle is missing (0) or the offset has
    // moved, this pointer lands in unrelated memory and writing it every frame
    // would corrupt the game, so verify the page is committed and writable.
    const uintptr_t clientBase = modules.getModule("client");
    if (clientBase == 0)
        return;

    QAngle_t* viewangles = reinterpret_cast<QAngle_t*>(clientBase + 0x23E2C98); // dwViewAngles - build 14181
    if (!is_writable_address(viewangles))
    {
        return;
    }

    C_CSPlayerPawn* best_pawn = nullptr;
    float best_fov = Config::aimbot_fov;
    QAngle_t best_angle{};

    if (Config::aimbot)
    for (int i = 1; i <= nMaxHighestEntity; i++) {
        auto Entity = I::GameEntity->Instance->Get(i);
        if (!Entity)
            continue;

        if (!Entity->handle().valid())
            continue;

        SchemaClassInfoData_t* _class = nullptr;
        Entity->dump_class_info(&_class);
        if (!_class || !_class->szName)
            continue;

        const uint32_t hash = HASH(_class->szName);
        if (hash != HASH("CCSPlayerController"))
            continue;

        CCSPlayerController* Controller = reinterpret_cast<CCSPlayerController*>(Entity);
        if (!Controller->m_hPawn().valid() || Controller->IsLocalPlayer())
            continue;

        C_CSPlayerPawn* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(Controller->m_hPawn().index());
        if (!pawn)
            continue;

        if (pawn->getHealth() < 1)
            continue;

        if (Config::team_check && pawn->getTeam() == lp->getTeam())
            continue;

        Vector_t eye_pos = GetEntityEyePos(pawn);
        QAngle_t angle = CalcAngles(eye_pos, lep);

        angle.x *= -1.f;
        angle.y += 180.f;

        const float fov = GetFov(*viewangles, angle);
        if (!std::isfinite(fov) || fov > best_fov)
            continue;
        // Closest to the crosshair wins - don't just take the first match.
        best_fov = fov;
        best_pawn = pawn;
        best_angle = angle;
    }

    if (best_pawn) {
        best_angle.z = 0.f;
        best_angle = best_angle.Normalize();
        *viewangles = best_angle;
    }
}
