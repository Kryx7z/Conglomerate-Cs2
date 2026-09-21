#include "visuals.h"
#include <algorithm>
#include <string>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cfloat>
#include <utility>
#include "../../hooks/hooks.h"
#include "../../players/players.h"
#include "../../utils/memory/patternscan/patternscan.h"
#include "../../utils/memory/gaa/gaa.h"
#include "../../../../external/imgui/imgui.h"
#include "../../interfaces/interfaces.h"
#include "../../config/config.h"
#include "../../menu/menu.h"
using namespace Esp;

LocalPlayerCached cached_local;
std::vector<PlayerCache> cached_players;

namespace
{
    enum class BoneID : int
    {
        PELVIS = 1, SPINE1 = 3, SPINE2 = 4, NECK = 6, HEAD = 7,
        SHOULDER_L = 9, ELBOW_L = 10, HAND_L = 11,
        SHOULDER_R = 13, ELBOW_R = 14, HAND_R = 15,
        HIP_L = 17, KNEE_L = 18, FOOT_L = 19,
        HIP_R = 20, KNEE_R = 21, FOOT_R = 22
    };

    bool IsValidAddress(std::uintptr_t address)
    {
        return address > 0x10000 && address < 0x7FFFFFFFFFFF;
    }

    bool GetBonePosition(C_CSPlayerPawn* pawn, BoneID bone, Vector_t& position)
    {
        if (!pawn)
            return false;

        __try
        {
            const auto scene = reinterpret_cast<std::uintptr_t>(pawn->m_pGameSceneNode());
            if (!IsValidAddress(scene))
                return false;

            const auto boneArray = *reinterpret_cast<std::uintptr_t*>(scene + 0x140 + 0x80);
            if (!IsValidAddress(boneArray))
                return false;

            position = *reinterpret_cast<Vector_t*>(boneArray + static_cast<int>(bone) * 0x20);
            return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const char* GetWeaponDataName(C_CSWeaponBase* weapon)
    {
        if (!weapon || !H::oGetWeaponData)
            return "unknown";

        auto* data = weapon->Data();
        return data && data->m_szName() ? data->m_szName() : "unknown";
    }

    const char* GetActiveWeaponName(C_CSPlayerPawn* pawn)
    {
        const char* result = "unknown";
        if (!pawn || !H::oGetWeaponData)
            return result;

        __try
        {
            auto* services = pawn->m_pWeaponServices();
            if (!services)
                return result;

            const auto handle = services->m_hActiveWeapon();
            if (!handle.valid() || !I::GameEntity || !I::GameEntity->Instance)
                return result;

            auto* weapon = I::GameEntity->Instance->Get<C_CSWeaponBase>(handle.index());
            if (!weapon)
                return result;

            result = GetWeaponDataName(weapon);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return "unknown";
        }

        return result;
    }

    bool HasC4InInventory(C_CSPlayerPawn* pawn)
    {
        if (!pawn || !I::GameEntity || !I::GameEntity->Instance)
            return false;

        __try
        {
            auto* services = pawn->m_pWeaponServices();
            if (!services)
                return false;

            auto& inventory = services->m_hMyWeapons();
            if (!inventory.pElements || inventory.nSize == 0 || inventory.nSize > 64)
                return false;

            for (std::uint32_t i = 0; i < inventory.nSize; ++i)
            {
                const auto handle = inventory.pElements[i];
                if (!handle.valid())
                    continue;

                auto* weapon = I::GameEntity->Instance->Get<C_CSWeaponBase>(handle.index());
                const char* name = GetWeaponDataName(weapon);
                if (name && std::strstr(name, "c4"))
                    return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        return false;
    }

    std::string FormatWeaponName(const std::string& source)
    {
        if (source.empty() || source == "unknown")
            return {};

        std::string label = source;
        if (label.rfind("weapon_", 0) == 0)
            label.erase(0, 7);
        else if (label.rfind("item_", 0) == 0)
            label.erase(0, 5);

        for (char& character : label)
        {
            if (character == '_')
                character = '-';
            else
                character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }

        return label;
    }

    ImVec2 CalcEspTextSize(ImFont* font, float size, const char* text)
    {
        if (!font || !text || !*text)
            return {};

        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    }
}

void Visuals::init() {
    viewMatrix.viewMatrix = (viewmatrix_t*)M::getAbsoluteAddress(M::patternScan("client", "48 8D 0D ? ? ? ? 48 C1 E0 06"), 3, 0);
}

void Esp::cache()
{
    if (!I::EngineClient || !I::EngineClient->valid())
        return;

    if (!I::GameEntity || !I::GameEntity->Instance)
        return;

    const int nMaxHighestEntity = I::GameEntity->Instance->GetHighestEntityIndex();

    cached_players.clear();

    for (int i = 1; i <= nMaxHighestEntity; i++)
    {
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
        if (!Controller->m_hPawn().valid())
            continue;

        if (Controller->IsLocalPlayer()) {
            auto LocalPlayer = I::GameEntity->Instance->Get<C_CSPlayerPawn>(Controller->m_hPawn().index());
            if (!LocalPlayer) {
                cached_local.reset();
                continue;
            }

            cached_local.alive = LocalPlayer->m_iHealth() > 0;
            if (cached_local.alive) {
                cached_local.poisition = LocalPlayer->m_vOldOrigin();
                cached_local.health = LocalPlayer->m_iHealth();
                cached_local.handle = LocalPlayer->handle().index();
                cached_local.team = LocalPlayer->m_iTeamNum();
            }
            else {
                cached_local.reset();
            }
        }
        else {
            auto Player = I::GameEntity->Instance->Get<C_CSPlayerPawn>(Controller->m_hPawn().index());
            if (!Player || Player->m_iHealth() <= 0)
                continue;

            const int health = Player->m_iHealth();
            const char* name = Controller->m_sSanitizedPlayerName();
            const Vector_t position = Player->m_vOldOrigin();
            const Vector_t viewOffset = Player->m_vecViewOffset();
            const char* weaponName = GetActiveWeaponName(Player);
            const bool carriesC4 = (weaponName && std::strstr(weaponName, "c4") != nullptr) ||
                HasC4InInventory(Player);

            cached_players.emplace_back(Entity, Player, Player->handle(),
                none, health, name,
                weaponName, position, viewOffset, Player->m_iTeamNum(),
                Player->m_bIsScoped(), Player->m_flFlashDuration(), carriesC4);
        }
    }
}

void Visuals::esp() {
    // Only proceed if at least one ESP component is enabled
    if (!Config::esp && !Config::showHealth && !Config::espFill && !Config::showNameTags &&
        !Config::flags && !Config::skeleton) {
        return; // Exit early if no component is enabled
    }

    //@better example of getting local pawn
    if (!H::oGetLocalPlayer)
        return;

    C_CSPlayerPawn* localPawn = H::oGetLocalPlayer(0);
    if (!localPawn) {
        return;
    }

    if (cached_players.empty())
        return;

    for (const auto& Player : cached_players)
    {

        if (!Player.handle.valid() || Player.health <= 0 || Player.handle.index() == INVALID_EHANDLE_INDEX)
            continue;

        if (Config::teamCheck && (Player.team_num == cached_local.team))
            continue;

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        Vector_t feetPos = Player.position;
        Vector_t headPos = Player.position + Player.viewOffset;

        Vector_t feetScreen, headScreen;
        if (!viewMatrix.WorldToScreen(feetPos, feetScreen) ||
            !viewMatrix.WorldToScreen(headPos, headScreen))
            continue;

        float boxHeight = (feetScreen.y - headScreen.y) * 1.3f;
        float boxWidth = boxHeight / 2.0f;

        float centerX = (feetScreen.x + headScreen.x) / 2.0f;
        float boxX = centerX - (boxWidth / 2.0f);

        float boxY = headScreen.y - (boxHeight - (feetScreen.y - headScreen.y)) / 2.0f;

        // CS2/Source team numbers: 2 = Terrorist, 3 = Counter-Terrorist
        ImVec4 teamColor = (Player.team_num == 2) ? Config::espColorT : Config::espColorCT;

        ImVec4 espColorWithAlpha = teamColor;
        espColorWithAlpha.w = Config::espFillOpacity;
        ImU32 boxColor = ImGui::ColorConvertFloat4ToU32(teamColor);
        ImU32 fillColor = ImGui::ColorConvertFloat4ToU32(espColorWithAlpha);

        if (Config::skeleton)
        {
            const ImU32 skeletonColor = ImGui::ColorConvertFloat4ToU32(Config::skeletonColor);
            const std::pair<BoneID, BoneID> bones[] = {
                {BoneID::PELVIS, BoneID::SPINE1}, {BoneID::SPINE1, BoneID::SPINE2},
                {BoneID::SPINE2, BoneID::NECK}, {BoneID::NECK, BoneID::HEAD},
                {BoneID::NECK, BoneID::SHOULDER_L}, {BoneID::SHOULDER_L, BoneID::ELBOW_L},
                {BoneID::ELBOW_L, BoneID::HAND_L}, {BoneID::NECK, BoneID::SHOULDER_R},
                {BoneID::SHOULDER_R, BoneID::ELBOW_R}, {BoneID::ELBOW_R, BoneID::HAND_R},
                {BoneID::PELVIS, BoneID::HIP_L}, {BoneID::HIP_L, BoneID::KNEE_L},
                {BoneID::KNEE_L, BoneID::FOOT_L}, {BoneID::PELVIS, BoneID::HIP_R},
                {BoneID::HIP_R, BoneID::KNEE_R}, {BoneID::KNEE_R, BoneID::FOOT_R}
            };

            for (const auto& [from, to] : bones)
            {
                Vector_t fromWorld, toWorld, fromScreen, toScreen;
                if (GetBonePosition(Player.player, from, fromWorld) &&
                    GetBonePosition(Player.player, to, toWorld) &&
                    viewMatrix.WorldToScreen(fromWorld, fromScreen) &&
                    viewMatrix.WorldToScreen(toWorld, toScreen))
                {
                    drawList->AddLine(ImVec2(fromScreen.x, fromScreen.y),
                        ImVec2(toScreen.x, toScreen.y), skeletonColor, Config::skeletonThickness);
                }
            }
        }

        // ESP Fill
        if (Config::espFill) {
            drawList->AddRectFilled(
                ImVec2(boxX, boxY),
                ImVec2(boxX + boxWidth, boxY + boxHeight),
                fillColor
            );
        }

        if (Config::esp) {
            drawList->AddRect(
                ImVec2(boxX, boxY),
                ImVec2(boxX + boxWidth, boxY + boxHeight),
                boxColor,
                0.0f,
                0,
                Config::espThickness
            );
        }

        const float espFontSize = std::clamp(ImGui::GetFontSize() * 0.65f, 8.0f, 10.0f);
        ImFont* espFont = ImGui::GetFont();

        if (Config::flags)
        {
            float flagY = boxY;
            const auto drawFlag = [&](const std::string& text, ImU32 color)
            {
                if (text.empty())
                    return;

                drawList->AddText(espFont, espFontSize,
                    ImVec2(boxX + boxWidth + 6.0f, flagY), color, text.c_str());
                flagY += espFontSize + 1.0f;
            };

            if (Player.scoped)
                drawFlag("SCOPE", IM_COL32(80, 150, 255, 255));
            if (Player.flashDuration > 0.0f && std::isfinite(Player.flashDuration))
                drawFlag("BLIND", IM_COL32(255, 220, 60, 255));
        }

        // Health Bar
        if (Config::showHealth) {
            const int health = std::clamp(Player.health, 0, 100);
            const float healthHeight = boxHeight * (static_cast<float>(health) / 100.0f);
            const float barWidth = 1.25f;
            float barX = boxX - (barWidth + 2);
            float barY = boxY + (boxHeight - healthHeight);

            drawList->AddRectFilled(
                ImVec2(barX, boxY),
                ImVec2(barX + barWidth, boxY + boxHeight),
                IM_COL32(70, 70, 70, 255)
            );

            ImU32 healthColor = IM_COL32(
                static_cast<int>((100 - health) * 2.55f),
                static_cast<int>(health * 2.55f),
                0,
                255
            );
            drawList->AddRectFilled(
                ImVec2(barX, barY),
                ImVec2(barX + barWidth, barY + healthHeight),
                healthColor
            );

        }

        if (Config::showNameTags) {
            std::string playerName = Player.name;
            const ImVec2 nameSize = CalcEspTextSize(espFont, espFontSize, playerName.c_str());

            float nameX = boxX + (boxWidth - nameSize.x) / 2;
            float nameY = boxY - nameSize.y - 2;

            drawList->AddText(espFont, espFontSize, ImVec2(nameX + 1, nameY + 1),
                IM_COL32(0, 0, 0, 255), playerName.c_str());
            drawList->AddText(espFont, espFontSize, ImVec2(nameX, nameY),
                IM_COL32(255, 255, 255, 255), playerName.c_str());
        }

        if (Config::flags)
        {
            const auto weaponLabel = FormatWeaponName(Player.weapon_name);
            if (!weaponLabel.empty())
            {
                const ImVec2 weaponSize = CalcEspTextSize(espFont, espFontSize, weaponLabel.c_str());
                const float weaponX = centerX - weaponSize.x / 2.0f;
                const float weaponY = boxY + boxHeight + 2.0f;
                drawList->AddText(espFont, espFontSize, ImVec2(weaponX, weaponY),
                    IM_COL32(255, 255, 255, 255), weaponLabel.c_str());
            }
        }

        if (Config::flags && Player.carriesC4)
        {
            constexpr ImU32 c4Color = IM_COL32(255, 0, 0, 255);
            const char* c4Text = "C4";
            const ImVec2 c4Size = CalcEspTextSize(espFont, espFontSize, c4Text);
            const float c4X = boxX + (boxWidth - c4Size.x) / 2.0f;
            const float c4BaseY = Config::showNameTags
                ? boxY - c4Size.y - 2.0f
                : boxY - espFontSize - 2.0f;
            const float c4Y = Config::showNameTags
                ? c4BaseY - c4Size.y - 2.0f
                : c4BaseY;
            drawList->AddText(espFont, espFontSize, ImVec2(c4X + 1.0f, c4Y + 1.0f),
                IM_COL32(0, 0, 0, 255), c4Text);
            drawList->AddText(espFont, espFontSize, ImVec2(c4X, c4Y), c4Color, c4Text);
        }
    }
}
