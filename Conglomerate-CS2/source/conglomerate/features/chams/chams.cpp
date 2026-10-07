#include <algorithm>
#include <array>
#include <limits>
#include "chams.h"
#include "../../hooks/hooks.h"
#include "../../config/config.h"
#include "../../../../external/imgui/imgui.h"
#include "../../utils/math/utlstronghandle/utlstronghandle.h"
#include "../../utils/memory/seh_diagnostics.h"
#include "../../utils/memory/safe_memory.h"
#include "../../utils/debug_console.h"
#include "../../../cs2/entity/C_Material/C_Material.h"
#include "../../interfaces/interfaces.h"
#include "../../interfaces/CGameEntitySystem/CGameEntitySystem.h"
#include "../../../cs2/datatypes/keyvalues/keyvalues.h"
#include "../../../cs2/datatypes/cutlbuffer/cutlbuffer.h"

static bool SafeCallCreateMaterial(CStrongHandle<CMaterial2>* pOut, const char* name, CKeyValues3* pKeyValues3)
{
    if (!I::CreateMaterial || !I::MaterialSystem2)
        return false;

    __try
    {
        I::CreateMaterial(I::MaterialSystem2, pOut, name, pKeyValues3, nullptr, 1U);
        return pOut && *pOut != nullptr;
    }
    __except (SehDiagnostics::handle("chams.create_material"))
    {
        return false;
    }
}

CStrongHandle<CMaterial2> chams::create(const char* name, const char szVmatBuffer[])
{
    CKeyValues3* pKeyValues3 = CKeyValues3::create_material_from_resource();

    if (!pKeyValues3)
    {
        return {};
    }

    if (!pKeyValues3->LoadFromBuffer(szVmatBuffer))
    {
        delete pKeyValues3;
        return {};
    }

    CStrongHandle<CMaterial2> pCustomMaterial = {};

    SafeCallCreateMaterial(&pCustomMaterial, name, pKeyValues3);

    CKeyValues3::destroy_material_resource(pKeyValues3);

    return pCustomMaterial;
}

struct resource_material_t
{
    CStrongHandle<CMaterial2> mat;
    CStrongHandle<CMaterial2> mat_invs;
};

static resource_material_t resourceMaterials[ChamsType::MAXCOUNT];
bool chams::Materials::init()
{
    resourceMaterials[FLAT].mat = create("materials/dev/flat.vmat", R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
        {
            Shader = "csgo_unlitgeneric.vfx"
        
            F_IGNOREZ = 0
             F_DISABLE_Z_WRITE = 0
            F_DISABLE_Z_BUFFERING = 0
            F_BLEND_MODE = 1
            F_TRANSLUCENT = 1
            F_RENDER_BACKFACES = 0

            g_vColorTint = [1.000000, 1.000000, 1.000000, 1.000000]
            g_vGlossinessRange = [0.000000, 1.000000, 0.000000, 0.000000]
            g_vNormalTexCoordScale = [1.000000, 1.000000, 0.000000, 0.000000]
            g_vTexCoordOffset = [0.000000, 0.000000, 0.000000, 0.000000]
            g_vTexCoordScale = [1.000000, 1.000000, 0.000000, 0.000000]
            g_tColor = resource:"materials/dev/primary_white_color_tga_f7b257f6.vtex"
            g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
        })");

    resourceMaterials[FLAT].mat_invs = create("materials/dev/flat_i.vmat", R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
        {
            Shader = "csgo_unlitgeneric.vfx"
            F_IGNOREZ = 1
            F_DISABLE_Z_WRITE = 1
            F_DISABLE_Z_BUFFERING = 1
            F_BLEND_MODE = 1
            F_TRANSLUCENT = 1
            g_vColorTint = [1.000000, 1.000000, 1.000000, 0.000000]
            g_vGlossinessRange = [0.000000, 1.000000, 0.000000, 0.000000]
            g_vNormalTexCoordScale = [1.000000, 1.000000, 0.000000, 0.000000]
            g_vTexCoordOffset = [0.000000, 0.000000, 0.000000, 0.000000]
            g_vTexCoordScale = [1.000000, 1.000000, 0.000000, 0.000000]
            g_tColor = resource:"materials/dev/primary_white_color_tga_f7b257f6.vtex"
            g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
        })");

    resourceMaterials[ILLUMINATE].mat = create("conglomerate_illuminate.vmat", R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
	shader = "csgo_complex.vfx"

	F_SELF_ILLUM = 1
	F_PAINT_VERTEX_COLORS = 1
	F_TRANSLUCENT = 1

	g_vColorTint = [ 1.000000, 1.000000, 1.000000, 1.000000 ]
	g_flSelfIllumScale = [ 3.000000, 3.000000, 3.000000, 3.000000 ]
	g_flSelfIllumBrightness = [ 3.000000, 3.000000, 3.000000, 3.000000 ]
    g_vSelfIllumTint = [ 10.000000, 10.000000, 10.000000, 10.000000 ]

	g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
	g_tNormal = resource:"materials/default/default_mask_tga_fde710a5.vtex"
	g_tSelfIllumMask = resource:"materials/default/default_mask_tga_fde710a5.vtex"
	TextureAmbientOcclusion = resource:"materials/debug/particleerror.vtex"
	g_tAmbientOcclusion = resource:"materials/debug/particleerror.vtex"
})");

    resourceMaterials[ILLUMINATE].mat_invs = create("conglomerate_illuminate_invs.vmat", R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
	shader = "csgo_complex.vfx"

	F_SELF_ILLUM = 1
	F_PAINT_VERTEX_COLORS = 1
	F_TRANSLUCENT = 1
    F_DISABLE_Z_BUFFERING = 1

	g_vColorTint = [ 1.000000, 1.000000, 1.000000, 1.000000 ]
	g_flSelfIllumScale = [ 3.000000, 3.000000, 3.000000, 3.000000 ]
	g_flSelfIllumBrightness = [ 3.000000, 3.000000, 3.000000, 3.000000 ]
    g_vSelfIllumTint = [ 10.000000, 10.000000, 10.000000, 10.000000 ]

	g_tColor = resource:"materials/default/default_mask_tga_fde710a5.vtex"
	g_tNormal = resource:"materials/default/default_mask_tga_fde710a5.vtex"
	g_tSelfIllumMask = resource:"materials/default/default_mask_tga_fde710a5.vtex"
	TextureAmbientOcclusion = resource:"materials/debug/particleerror.vtex"
	g_tAmbientOcclusion = resource:"materials/debug/particleerror.vtex"
})");

    resourceMaterials[GLOW].mat = create("conglomerate_glow.vmat", R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
				shader = "csgo_effects.vfx"
                g_flFresnelExponent = 7.0
                g_flFresnelFalloff = 10.0
                g_flFresnelMax = 0.1
                g_flFresnelMin = 1.0
				g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
                g_tMask1 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tMask2 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tMask3 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tSceneDepth = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_flToolsVisCubemapReflectionRoughness = 1.0
                g_flBeginMixingRoughness = 1.0
                g_vColorTint = [ 1.000000, 1.000000, 1.000000, 0 ]
                F_IGNOREZ = 0
	            F_TRANSLUCENT = 1

                F_DISABLE_Z_WRITE = 0
                F_DISABLE_Z_BUFFERING = 1
                F_RENDER_BACKFACES = 0
})");

    resourceMaterials[GLOW].mat_invs = create("conglomerate_glow_invs.vmat", R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
				shader = "csgo_effects.vfx"
                g_flFresnelExponent = 7.0
                g_flFresnelFalloff = 10.0
                g_flFresnelMax = 0.1
                g_flFresnelMin = 1.0
				g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
                g_tMask1 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tMask2 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tMask3 = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_tSceneDepth = resource:"materials/default/default_mask_tga_fde710a5.vtex"
                g_flToolsVisCubemapReflectionRoughness = 1.0
                g_flBeginMixingRoughness = 1.0
                g_vColorTint = [ 1.000000, 1.000000, 1.000000, 0 ]
                F_IGNOREZ = 1
	            F_TRANSLUCENT = 1

                F_DISABLE_Z_WRITE = 1
                F_DISABLE_Z_BUFFERING = 1
                F_RENDER_BACKFACES = 0
})");

    const bool allCreated =
        resourceMaterials[FLAT].mat != nullptr &&
        resourceMaterials[FLAT].mat_invs != nullptr &&
        resourceMaterials[ILLUMINATE].mat != nullptr &&
        resourceMaterials[ILLUMINATE].mat_invs != nullptr &&
        resourceMaterials[GLOW].mat != nullptr &&
        resourceMaterials[GLOW].mat_invs != nullptr;
	DebugConsole::logf("[materials] create results flat=%d/%d illuminate=%d/%d glow=%d/%d",
		resourceMaterials[FLAT].mat != nullptr, resourceMaterials[FLAT].mat_invs != nullptr,
		resourceMaterials[ILLUMINATE].mat != nullptr, resourceMaterials[ILLUMINATE].mat_invs != nullptr,
		resourceMaterials[GLOW].mat != nullptr, resourceMaterials[GLOW].mat_invs != nullptr);

    return allCreated;
}

static C_CSPlayerPawn* findPlayerPawn(C_BaseEntity* renderEntity, CBaseHandle sceneOwner)
{
    if (!renderEntity || !I::GameEntity || !I::GameEntity->Instance)
        return nullptr;

    if (renderEntity->IsPlayerController())
    {
        auto* controller = reinterpret_cast<CCSPlayerController*>(renderEntity);
        const CBaseHandle pawnHandle = controller->m_hPlayerPawn();
        if (pawnHandle.valid())
        {
            C_CSPlayerPawn* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(pawnHandle);
            if (pawn)
                return pawn;
        }
    }

    for (int i = 1; i <= 64; ++i)
    {
        C_BaseEntity* entity = I::GameEntity->Instance->Get(i);
        if (!entity || !entity->IsPlayerController())
            continue;

        auto* controller = reinterpret_cast<CCSPlayerController*>(entity);
        const CBaseHandle pawnHandle = controller->m_hPlayerPawn();
        if (!pawnHandle.valid())
            continue;

        if (sceneOwner.valid() && pawnHandle == sceneOwner)
        {
            C_CSPlayerPawn* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(pawnHandle);
            if (pawn)
                return pawn;
        }

        C_CSPlayerPawn* pawn = I::GameEntity->Instance->Get<C_CSPlayerPawn>(pawnHandle);
        if (pawn == renderEntity)
            return pawn;
    }

    return nullptr;
}

enum class SceneOwnerKind : std::uint8_t
{
    UNKNOWN,
    PLAYER_PAWN,
    HANDS,
    VIEWMODEL
};

static SceneOwnerKind getSceneOwnerKind(C_BaseEntity* entity, const char** className = nullptr) noexcept
{
    if (className)
        *className = nullptr;
    if (!entity)
        return SceneOwnerKind::UNKNOWN;

    std::uintptr_t identity = 0;
    std::uintptr_t entityClass = 0;
    std::uintptr_t classInfo = 0;
    const char* name = nullptr;
    if (!SafeMemory::read(reinterpret_cast<std::uintptr_t>(entity) + 0x10u, identity) || !identity ||
        !SafeMemory::read(identity + 0x8u, entityClass) || !entityClass ||
        !SafeMemory::read(entityClass + 0x58u, classInfo) || !classInfo ||
        !SafeMemory::read(classInfo + 0x8u, name) || !name)
        return SceneOwnerKind::UNKNOWN;

    const std::uint32_t classHash = hash_32_fnv1a_const(name);
    if (className)
        *className = name;
    if (classHash == hash_32_fnv1a_const("C_CSPlayerPawn") ||
            classHash == hash_32_fnv1a_const("C_CSPlayerPawnBase"))
        return SceneOwnerKind::PLAYER_PAWN;

    if (classHash == hash_32_fnv1a_const("C_ViewmodelAttachmentModel") ||
            classHash == hash_32_fnv1a_const("C_CS2HudModelArms"))
        return SceneOwnerKind::HANDS;

    if (classHash == hash_32_fnv1a_const("C_CSGOViewModel") ||
            classHash == hash_32_fnv1a_const("C_CS2HudModelWeapon"))
        return SceneOwnerKind::VIEWMODEL;

    return SceneOwnerKind::UNKNOWN;
}

ChamsEntity chams::GetTargetType(C_BaseEntity* render_ent, CBaseHandle sceneOwner) noexcept {
    if (!render_ent)
        return ChamsEntity::INVALID;

    if (!H::oGetLocalPlayer)
        return ChamsEntity::INVALID;

    auto local = H::oGetLocalPlayer(0);
    if (!local)
        return ChamsEntity::INVALID;

    const SceneOwnerKind ownerKind = getSceneOwnerKind(render_ent);
    auto* player = ownerKind == SceneOwnerKind::PLAYER_PAWN
        ? reinterpret_cast<C_CSPlayerPawn*>(render_ent)
        : findPlayerPawn(render_ent, sceneOwner);
    if (player)
    {
        const int health = player->getHealth();
        if (health <= 0)
        {
            DebugConsole::rateLimited("chams.target.nonpositive_health",
                "[runtime] GeneratePrimitives resolved a pawn but rejected it: health=%d owner_index=%d",
                health, sceneOwner.valid() ? sceneOwner.index() : -1);
            return ChamsEntity::INVALID;
        }

        return player->getTeam() == local->getTeam()
            ? ChamsEntity::TEAM
            : ChamsEntity::ENEMY;
    }

    if (ownerKind == SceneOwnerKind::HANDS)
        return ChamsEntity::HANDS;

    if (ownerKind == SceneOwnerKind::VIEWMODEL)
        return ChamsEntity::VIEWMODEL;

    return ChamsEntity::INVALID;
}

CMaterial2* GetMaterial(int type, bool invisible)
{
    if (type < 0 || type >= ChamsType::MAXCOUNT)
        return nullptr;

    return invisible ? resourceMaterials[type].mat_invs : resourceMaterials[type].mat;
}

struct PrimitiveOutputBuffer
{
    std::uintptr_t fixedData;
    std::int32_t fixedCapacity;
    std::int32_t fixedCount;
    std::int32_t overflowCount;
    std::uint32_t reserved;
    std::uintptr_t overflowData;
    std::int32_t overflowCapacity;
    std::uint32_t overflowFlags;
};

static_assert(sizeof(PrimitiveOutputBuffer) == 0x28);
static_assert(sizeof(CMeshData) == 0x70, "CMeshData primitive layout changed");

static constexpr std::uintptr_t kPrimitiveDrawOrderOffset = 0x58u;
static constexpr std::uintptr_t kPrimitiveFlagsOffset = 0x62u;
static constexpr std::uint16_t kPrimitiveDrawLast = 0x8u;

static bool IsValidPrimitiveBuffer(const PrimitiveOutputBuffer& buffer)
{
    if (buffer.fixedCapacity < 0 || buffer.overflowCapacity < 0 ||
        buffer.fixedCount < 0 || buffer.overflowCount < 0 ||
        buffer.fixedCount > buffer.fixedCapacity || buffer.overflowCount > buffer.overflowCapacity ||
        (buffer.fixedCount && !buffer.fixedData) || (buffer.overflowCount && !buffer.overflowData))
        return false;

    constexpr std::int32_t maxPrimitiveCount = 1 << 20;
    return buffer.fixedCount <= maxPrimitiveCount - buffer.overflowCount;
}

static std::int32_t PrimitiveCount(const PrimitiveOutputBuffer& buffer)
{
    return buffer.fixedCount + buffer.overflowCount;
}

static std::uintptr_t PrimitiveAt(const PrimitiveOutputBuffer& buffer, std::int32_t index)
{
    if (index < 0 || index >= PrimitiveCount(buffer))
        return 0;

    if (index < buffer.fixedCount)
        return buffer.fixedData + static_cast<std::uintptr_t>(index) * 0x70u;

    return buffer.overflowData + static_cast<std::uintptr_t>(index - buffer.fixedCount) * 0x70u;
}

using GeneratePrimitivesFn = void(__fastcall*)(void*, void*, void*, void*);

static bool AppendChamsLayer(GeneratePrimitivesFn original, void* thisptr, void* sceneObject,
    void* sceneView, void* primitiveBuffer, CMaterial2* material, const ImVec4& color,
    bool drawLast, bool& originalCalled)
{
    if (!original || !material)
        return false;

    PrimitiveOutputBuffer before{};
    if (!SafeMemory::read(reinterpret_cast<std::uintptr_t>(primitiveBuffer), before) ||
        !IsValidPrimitiveBuffer(before))
        return false;

    original(thisptr, sceneObject, sceneView, primitiveBuffer);
    originalCalled = true;

    PrimitiveOutputBuffer after{};
    if (!SafeMemory::read(reinterpret_cast<std::uintptr_t>(primitiveBuffer), after) ||
        !IsValidPrimitiveBuffer(after))
        return true;

    const auto begin = PrimitiveCount(before);
    const auto end = PrimitiveCount(after);
    if (end <= begin)
        return true;

    const Color replacement{
        static_cast<std::uint8_t>(color.x * 255.0f),
        static_cast<std::uint8_t>(color.y * 255.0f),
        static_cast<std::uint8_t>(color.z * 255.0f),
        static_cast<std::uint8_t>(color.w * 255.0f)
    };
    const auto materialAddress = reinterpret_cast<std::uintptr_t>(material);
    for (auto i = begin; i < end; ++i)
    {
        const auto primitive = PrimitiveAt(after, i);
        if (!primitive)
            continue;
        SafeMemory::write(primitive + 0x20u, materialAddress);
        SafeMemory::write(primitive + 0x28u, materialAddress);
        SafeMemory::write(primitive + 0x50u, replacement);

        if (drawLast)
        {
            std::uint16_t flags{};
            if (SafeMemory::read(primitive + kPrimitiveFlagsOffset, flags))
                SafeMemory::write(primitive + kPrimitiveFlagsOffset,
                    static_cast<std::uint16_t>(flags | kPrimitiveDrawLast));

            std::int32_t drawOrder{};
            if (SafeMemory::read(primitive + kPrimitiveDrawOrderOffset, drawOrder) &&
                drawOrder < (std::numeric_limits<std::int32_t>::max)())
                SafeMemory::write(primitive + kPrimitiveDrawOrderOffset, drawOrder + 1);
        }
    }
    return true;
}

void __fastcall chams::generatePrimitivesHook(void* thisptr, void* sceneObject, void* sceneView, void* primitiveBuffer)
{
    const auto original = H::GeneratePrimitives.GetOriginal();
    if (!original)
        return;

    DebugConsole::once("chams.generate.first", "[runtime] GeneratePrimitives detour reached; scene=%p buffer=%p", sceneObject, primitiveBuffer);

    bool originalCalled = false;
    CBaseHandle ownerHandle;
    C_BaseEntity* owner = nullptr;
    if (!sceneObject || !primitiveBuffer)
        DebugConsole::rateLimited("chams.generate.arguments", "[runtime] GeneratePrimitives skipped owner lookup: scene or primitive buffer is null");
    else if (!SafeMemory::read(reinterpret_cast<std::uintptr_t>(sceneObject) + 0xC0u, ownerHandle))
        DebugConsole::rateLimited("chams.generate.owner_read", "[runtime] GeneratePrimitives could not read scene owner handle at +0xC0");
    else if (!ownerHandle.valid())
        DebugConsole::rateLimited("chams.generate.owner_invalid", "[runtime] GeneratePrimitives scene owner handle is invalid");
    else if (!I::GameEntity || !I::GameEntity->Instance)
        DebugConsole::rateLimited("chams.generate.entity_system", "[runtime] GeneratePrimitives owner lookup skipped: entity system unavailable");
    else
    {
        owner = I::GameEntity->Instance->Get(ownerHandle);
        if (!owner)
            DebugConsole::rateLimited("chams.generate.owner_unresolved",
                "[runtime] GeneratePrimitives could not resolve scene owner handle_index=%d", ownerHandle.index());
    }

    const auto target = owner ? chams::GetTargetType(owner, ownerHandle) : ChamsEntity::INVALID;
    if (owner && target == ChamsEntity::INVALID)
    {
        const char* className = nullptr;
        const auto kind = getSceneOwnerKind(owner, &className);
        DebugConsole::rateLimited("chams.generate.unclassified",
            "[runtime] GeneratePrimitives owner did not classify: entity=%p handle_index=%d class=%s kind=%d",
            owner, ownerHandle.index(), className ? className : "null", static_cast<int>(kind));
    }
    auto apply = [&](int type, bool invisible, const ImVec4& tint)
    {
        CMaterial2* material = GetMaterial(type, invisible);
        if (!material)
            return;
        AppendChamsLayer(original, thisptr, sceneObject, sceneView, primitiveBuffer, material, tint,
            !invisible, originalCalled);
    };

    if (target == ChamsEntity::ENEMY)
    {
        DebugConsole::once("chams.generate.enemy", "[runtime] GeneratePrimitives classified enemy; visible=%d occluded=%d material=%d", Config::enemyChams, Config::enemyChamsInvisible, Config::chamsMaterial);
        if (Config::enemyChamsInvisible)
            apply(Config::chamsMaterial, true, Config::colVisualChamsIgnoreZ);
        if (Config::enemyChams)
            apply(Config::chamsMaterial, false, Config::colVisualChams);
    }
    else if (target == ChamsEntity::TEAM)
    {
        DebugConsole::once("chams.generate.team", "[runtime] GeneratePrimitives classified teammate; visible=%d occluded=%d material=%d", Config::teamChams, Config::teamChamsInvisible, Config::chamsMaterial);
        if (Config::teamChamsInvisible)
            apply(Config::chamsMaterial, true, Config::teamcolVisualChamsIgnoreZ);
        if (Config::teamChams)
            apply(Config::chamsMaterial, false, Config::teamcolVisualChams);
    }
    else if (target == ChamsEntity::HANDS && Config::armChams)
    {
        DebugConsole::once("chams.generate.hands", "[runtime] GeneratePrimitives classified arms; material=%d", Config::armChamsMaterial);
        apply(Config::armChamsMaterial, false, Config::colArmChams);
    }
    else if (target == ChamsEntity::VIEWMODEL && Config::viewmodelChams)
    {
        DebugConsole::once("chams.generate.viewmodel", "[runtime] GeneratePrimitives classified weapon; material=%d", Config::viewmodelChamsMaterial);
        apply(Config::viewmodelChamsMaterial, false, Config::colViewmodelChams);
    }

    if (!originalCalled)
        original(thisptr, sceneObject, sceneView, primitiveBuffer);
}

struct MeshDrawState
{
    CMaterial2* material;
    CMaterial2* material2;
    Color color;
};

static constexpr int kMaxCapturedMeshCount = 1024;

static bool CaptureMeshDrawState(CMeshData* meshes, int meshCount, MeshDrawState* states)
{
    if (!meshes || !states || meshCount < 1 || meshCount > kMaxCapturedMeshCount)
        return false;

    const auto base = reinterpret_cast<std::uintptr_t>(meshes);
    for (int i = 0; i < meshCount; ++i)
    {
        const auto mesh = base + static_cast<std::uintptr_t>(i) * sizeof(CMeshData);
        auto& state = states[i];
        if (!SafeMemory::read(mesh + offsetof(CMeshData, pMaterial), state.material) ||
            !SafeMemory::read(mesh + offsetof(CMeshData, pMaterial2), state.material2) ||
            !SafeMemory::read(mesh + offsetof(CMeshData, color), state.color))
        {
            return false;
        }
    }
    return true;
}

static void RestoreMeshDrawState(CMeshData* meshes, const MeshDrawState* states, int meshCount) noexcept
{
    if (!meshes || !states || meshCount < 1 || meshCount > kMaxCapturedMeshCount)
        return;

    const auto base = reinterpret_cast<std::uintptr_t>(meshes);
    for (int i = 0; i < meshCount; ++i)
    {
        const auto mesh = base + static_cast<std::uintptr_t>(i) * sizeof(CMeshData);
        const auto& state = states[i];
        SafeMemory::write(mesh + offsetof(CMeshData, pMaterial), state.material);
        SafeMemory::write(mesh + offsetof(CMeshData, pMaterial2), state.material2);
        SafeMemory::write(mesh + offsetof(CMeshData, color), state.color);
    }
}

using DrawArrayFn = decltype(H::DrawArray.GetOriginal());

std::uintptr_t __fastcall chams_hook_impl(DrawArrayFn original, void* a1, void* a2, CMeshData* pMeshScene, int nMeshCount, int a5, void* a6, void* a7, void* a8, bool allowOverrides, volatile bool& originalInvoked)
{
    if (!original)
        return 0;

    if (allowOverrides)
        H::applyNightPrimitiveColor(pMeshScene, nMeshCount);

    originalInvoked = true;
    return original(a1, a2, pMeshScene, nMeshCount, a5, a6, a7, a8);
}

static std::uintptr_t invokeChamsHookSafely(DrawArrayFn original, void* a1, void* a2, CMeshData* pMeshScene, int nMeshCount, int a5, void* a6, void* a7, void* a8, bool allowOverrides)
{
	volatile bool originalInvoked = false;
    __try
    {
		return chams_hook_impl(original, a1, a2, pMeshScene, nMeshCount, a5, a6, a7, a8, allowOverrides, originalInvoked);
    }
    __except (SehDiagnostics::handle("chams.hook", GetExceptionInformation()))
    {
		if (!originalInvoked && original)
			return original(a1, a2, pMeshScene, nMeshCount, a5, a6, a7, a8);
		return 0;
    }
}

std::uintptr_t __fastcall chams::hook(void* a1, void* a2, CMeshData* pMeshScene, int nMeshCount, int a5, void* a6, void* a7, void* a8)
{
	DebugConsole::once("chams.hook.first", "[runtime] DrawArray detour reached; mesh count=%d mesh pointer=%p", nMeshCount, pMeshScene);
	const auto original = H::DrawArray.GetOriginal();
	const bool overridesRequested = Config::Night;
	std::array<MeshDrawState, kMaxCapturedMeshCount> originalMeshState{};
	const bool allowOverrides = !overridesRequested ||
		CaptureMeshDrawState(pMeshScene, nMeshCount, originalMeshState.data());
	if (overridesRequested && !allowOverrides)
		DebugConsole::rateLimited("chams.mesh_state_unavailable", "[runtime] scene overrides skipped: mesh state could not be captured (count=%d)", nMeshCount);
	const auto drawResult = invokeChamsHookSafely(original, a1, a2, pMeshScene, nMeshCount, a5, a6, a7, a8, allowOverrides);
	if (overridesRequested && allowOverrides)
		RestoreMeshDrawState(pMeshScene, originalMeshState.data(), nMeshCount);
	return drawResult;
}
