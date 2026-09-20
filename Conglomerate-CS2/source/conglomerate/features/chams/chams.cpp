#include <algorithm>
#include "chams.h"
#include "../../hooks/hooks.h"
#include "../../config/config.h"
#include "../../../../external/imgui/imgui.h"
#include "../../utils/math/utlstronghandle/utlstronghandle.h"
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
        // Match the working ChamsV4 ABI: the material-system interface is the
        // first argument, followed by the strong-handle output and KV3 object.
        I::CreateMaterial(I::MaterialSystem2, pOut, name, pKeyValues3, nullptr, 1U);
        return pOut && *pOut != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
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

    // Used to return true unconditionally, so a failed creation produced chams
    // that silently rendered nothing.
    const bool allCreated =
        resourceMaterials[FLAT].mat != nullptr &&
        resourceMaterials[FLAT].mat_invs != nullptr &&
        resourceMaterials[ILLUMINATE].mat != nullptr &&
        resourceMaterials[ILLUMINATE].mat_invs != nullptr &&
        resourceMaterials[GLOW].mat != nullptr &&
        resourceMaterials[GLOW].mat_invs != nullptr;

    return allCreated;
}

ChamsEntity chams::GetTargetType(C_BaseEntity* render_ent) noexcept {
    if (!H::oGetLocalPlayer)
        return ChamsEntity::INVALID;

    auto local = H::oGetLocalPlayer(0);
    if (!local)
        return ChamsEntity::INVALID;

    if (render_ent->IsViewmodelAttachment())
        return ChamsEntity::HANDS;

    if (render_ent->IsViewmodel())
        return ChamsEntity::VIEWMODEL;

    if (!render_ent->IsBasePlayer() && !render_ent->IsPlayerController())
        return ChamsEntity::INVALID;

    auto player = (C_CSPlayerPawn*)render_ent;
    if (!player)
        return ChamsEntity::INVALID;

    auto alive = player->m_iHealth() > 0;
    if (!alive)
        return ChamsEntity::INVALID;

    if (player->m_iTeamNum() == local->m_iTeamNum())
        return ChamsEntity::INVALID;

    return ChamsEntity::ENEMY;
}

CMaterial2* GetMaterial(int type, bool invisible)
{
    // Config::chamsMaterial is round-tripped through the config json, so it can
    // come back out of range. resourceMaterials only has MAXCOUNT entries.
    if (type < 0 || type >= ChamsType::MAXCOUNT)
        return nullptr;

    return invisible ? resourceMaterials[type].mat_invs : resourceMaterials[type].mat;
}

static void ApplyMeshMaterial(CMeshData* firstMesh, int meshCount, CMaterial2* material, const ImVec4& color)
{
    if (!firstMesh || meshCount < 1 || !material)
        return;

    for (int i = 0; i < meshCount; ++i)
    {
        CMeshData* mesh = firstMesh + i;
        mesh->pMaterial = material;
        mesh->pMaterial2 = material;
        mesh->color.r = static_cast<uint8_t>(color.x * 255.0f);
        mesh->color.g = static_cast<uint8_t>(color.y * 255.0f);
        mesh->color.b = static_cast<uint8_t>(color.z * 255.0f);
        mesh->color.a = static_cast<uint8_t>(color.w * 255.0f);
    }
}

void __fastcall chams_hook_impl(void* a1, void* a2, CMeshData* pMeshScene, int nMeshCount, void* pSceneView, void* pSceneLayer, void* pUnk, void* pUnk2)
{
    static auto original = H::DrawArray.GetOriginal();
    if (!original)
        return;

    if (!I::EngineClient || !I::EngineClient->valid())
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
    if (!I::GameEntity || !I::GameEntity->Instance)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
    if (!H::oGetLocalPlayer)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
    auto local_player = H::oGetLocalPlayer(0);
    if (!local_player)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
    if (!pMeshScene)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);

    if (!pMeshScene->pSceneAnimatableObject)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);

    if (nMeshCount < 1)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);

    CMeshData* render_data = pMeshScene;
    if (!render_data)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);

    if (!render_data->pSceneAnimatableObject)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);

    auto render_ent = render_data->pSceneAnimatableObject->Owner();
    if (!render_ent.valid())
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);

    auto entity = I::GameEntity->Instance->Get(render_ent);
    if (!entity)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);

    const auto target = chams::GetTargetType(entity);

    if (target == ChamsEntity::VIEWMODEL && Config::viewmodelChams) {
        CMaterial2* mat = GetMaterial(Config::viewmodelChamsMaterial, false);
        if (!mat)
            return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
        ApplyMeshMaterial(pMeshScene, nMeshCount, mat, Config::colViewmodelChams);
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
    }

    if (target == ChamsEntity::HANDS && Config::armChams) {
        CMaterial2* mat = GetMaterial(Config::armChamsMaterial, false);
        if (!mat)
            return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
        ApplyMeshMaterial(pMeshScene, nMeshCount, mat, Config::colArmChams);
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
    }

    if (target != ENEMY)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);

    bool og = !Config::enemyChams && !Config::enemyChamsInvisible;
    if (og)
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);

    if (Config::enemyChamsInvisible) {
        CMaterial2* mat = GetMaterial(Config::chamsMaterial, true);
        if (mat) {
            ApplyMeshMaterial(pMeshScene, nMeshCount, mat, Config::colVisualChamsIgnoreZ);

            original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);

            if (!Config::enemyChams)
                return;
        }
    }

    if (Config::enemyChams) {
        CMaterial2* mat = GetMaterial(Config::chamsMaterial, false);
        if (!mat)
            return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
        ApplyMeshMaterial(pMeshScene, nMeshCount, mat, Config::colVisualChams);
        return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
    }

    // If we get here, neither chams type is enabled, so just render normally
    return original(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
}

void __fastcall chams::hook(void* a1, void* a2, CMeshData* pMeshScene, int nMeshCount, void* pSceneView, void* pSceneLayer, void* pUnk, void* pUnk2)
{
    __try
    {
        chams_hook_impl(a1, a2, pMeshScene, nMeshCount, pSceneView, pSceneLayer, pUnk, pUnk2);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // NOTE: deliberately NOT calling original() here - if this still crashes,
        // calling original() would too (that's what crashed originally, even on
        // the shortest code path). The DrawArray address itself has since been
        // independently re-verified as correct against a fresh signature dump,
        // so if this keeps crashing the issue is elsewhere (e.g. argument/
        // calling-convention mismatch) and needs live debugger verification.
    }
}
