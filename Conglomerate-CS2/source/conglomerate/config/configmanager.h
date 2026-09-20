#pragma once

#include <string>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "config.h"
#include "../keybinds/keybinds.h"
#include "../../../external/json/json.hpp"

namespace internal_config
{
    class ConfigManager
    {
    private:

        static std::filesystem::path GetConfigFolder()
        {
            char* userProfile = nullptr;
            size_t len = 0;
            errno_t err = _dupenv_s(&userProfile, &len, "USERPROFILE");

            std::filesystem::path folder;
            if (err != 0 || userProfile == nullptr || len == 0)
            {
                // _dupenv_s can still have handed back an allocation here (an
                // empty USERPROFILE gives len == 0), so release it either way.
                free(userProfile);
                folder = ".conglomerate";
            }
            else
            {
                folder = userProfile;
                free(userProfile);
                folder /= ".conglomerate";
            }
            folder /= "internal";

            std::error_code ec;
            std::filesystem::create_directories(folder, ec);

            return folder;
        }

        static std::filesystem::path GetConfigPath(const std::string& configName)
        {
            auto folder = GetConfigFolder();
            return folder / (configName + ".json");
        }

    public:

        static std::vector<std::string> ListConfigs()
        {
            std::vector<std::string> list;
            auto folder = GetConfigFolder();
            if (!std::filesystem::exists(folder))
                return list;

            // directory_iterator throws on an unreadable/permission-denied dir,
            // and this runs from the render hook.
            try
            {
                for (const auto& entry : std::filesystem::directory_iterator(folder))
                {
                    if (entry.is_regular_file())
                    {
                        auto path = entry.path();
                        if (path.extension() == ".json")
                        {
                            list.push_back(path.stem().string());
                        }
                    }
                }
            }
            catch (const std::filesystem::filesystem_error&)
            {
            }

            return list;
        }

        static void Save(const std::string& configName)
        {
            nlohmann::json j;
            j["esp"] = Config::esp;
            j["showHealth"] = Config::showHealth;
            j["teamCheck"] = Config::teamCheck;
            j["espFill"] = Config::espFill;
            j["espThickness"] = Config::espThickness;
            j["espFillOpacity"] = Config::espFillOpacity;

            j["fovEnabled"] = Config::fovEnabled;
            j["fov"] = Config::fov;
            j["watermark"] = Config::watermark;
            j["spectatorList"] = Config::spectatorList;

            j["flags"] = Config::flags;
            j["skeleton"] = Config::skeleton;
            j["skeletonThickness"] = Config::skeletonThickness;
            j["skeletonColor"] = {
                Config::skeletonColor.x,
                Config::skeletonColor.y,
                Config::skeletonColor.z,
                Config::skeletonColor.w
            };

            j["espColor"] = {
                Config::espColor.x,
                Config::espColor.y,
                Config::espColor.z,
                Config::espColor.w
            };
            j["espColorT"] = {
                Config::espColorT.x,
                Config::espColorT.y,
                Config::espColorT.z,
                Config::espColorT.w
            };
            j["espColorCT"] = {
                Config::espColorCT.x,
                Config::espColorCT.y,
                Config::espColorCT.z,
                Config::espColorCT.w
            };

            j["Night"] = Config::Night;
            j["NightColor"] = {
                Config::NightColor.x,
                Config::NightColor.y,
                Config::NightColor.z,
                Config::NightColor.w
            };

            j["armChams"] = Config::armChams;
            j["viewmodelChams"] = Config::viewmodelChams;
            j["armChamsMaterial"] = Config::armChamsMaterial;
            j["viewmodelChamsMaterial"] = Config::viewmodelChamsMaterial;

            j["armChams_color"] = {
                Config::colArmChams.x,
                Config::colArmChams.y,
                Config::colArmChams.z,
                Config::colArmChams.w
            };

            j["viewmodelChams_color"] = {
                Config::colViewmodelChams.x,
                Config::colViewmodelChams.y,
                Config::colViewmodelChams.z,
                Config::colViewmodelChams.w
            };

            j["aimbot"] = Config::aimbot;
            j["aimbot_fov"] = Config::aimbot_fov;
            j["aimbotKey"] = keybind.getKey(Config::aimbot);
            j["team_check"] = Config::team_check;
            j["antiflash"] = Config::antiflash;
            j["noSmoke"] = Config::noSmoke;
            j["fov_circle"] = Config::fov_circle;

            j["enemyChamsInvisible"] = Config::enemyChamsInvisible;
            j["enemyChams"] = Config::enemyChams;
            j["teamChams"] = Config::teamChams;
            j["teamChamsInvisible"] = Config::teamChamsInvisible;
            j["chamsMaterial"] = Config::chamsMaterial;
            j["colVisualChams"] = {
                Config::colVisualChams.x,
                Config::colVisualChams.y,
                Config::colVisualChams.z,
                Config::colVisualChams.w
            };
            j["colVisualChamsIgnoreZ"] = {
                Config::colVisualChamsIgnoreZ.x,
                Config::colVisualChamsIgnoreZ.y,
                Config::colVisualChamsIgnoreZ.z,
                Config::colVisualChamsIgnoreZ.w
            };
            j["teamcolVisualChamsIgnoreZ"] = {
                Config::teamcolVisualChamsIgnoreZ.x,
                Config::teamcolVisualChamsIgnoreZ.y,
                Config::teamcolVisualChamsIgnoreZ.z,
                Config::teamcolVisualChamsIgnoreZ.w
            };
            j["teamcolVisualChams"] = {
                Config::teamcolVisualChams.x,
                Config::teamcolVisualChams.y,
                Config::teamcolVisualChams.z,
                Config::teamcolVisualChams.w
            };
            j["fovCircleColor"] = {
                Config::fovCircleColor.x,
                Config::fovCircleColor.y,
                Config::fovCircleColor.z,
                Config::fovCircleColor.w
            };

            auto filePath = GetConfigPath(configName);
            std::ofstream ofs(filePath);
            if (ofs.is_open())
            {
                ofs << j.dump(4);
                ofs.close();
            }
        }

        static void Load(const std::string& configName)
        {
            // operator>> on a malformed file and value<T>() on a type mismatch
            // both throw. This is called from the render hook, so don't let a
            // hand-edited json take the frame down with it.
            try
            {
                LoadInternal(configName);
            }
            catch (const std::exception&)
            {
            }
        }

        static void LoadInternal(const std::string& configName)
        {
            auto filePath = GetConfigPath(configName);
            if (!std::filesystem::exists(filePath))
                return;

            std::ifstream ifs(filePath);
            if (!ifs.is_open())
                return;

            nlohmann::json j;
            ifs >> j;

            Config::esp = j.value("esp", false);
            Config::showHealth = j.value("showHealth", false);
            Config::teamCheck = j.value("teamCheck", false);
            Config::espFill = j.value("espFill", false);
            Config::espThickness = j.value("espThickness", 1.0f);
            Config::espFillOpacity = j.value("espFillOpacity", 0.5f);

            Config::fovEnabled = j.value("fovEnabled", false);
            Config::fov = std::clamp(j.value("fov", 90.0f), 20.0f, 160.0f);
            Config::watermark = j.value("watermark", true);
            Config::spectatorList = j.value("spectatorList", false);

            Config::flags = j.value("flags", false);
            Config::skeleton = j.value("skeleton", false);
            Config::skeletonThickness = std::clamp(j.value("skeletonThickness", 1.0f), 1.0f, 5.0f);
            if (j.contains("skeletonColor") && j["skeletonColor"].is_array() && j["skeletonColor"].size() == 4)
            {
                auto arr = j["skeletonColor"];
                Config::skeletonColor.x = arr[0].get<float>();
                Config::skeletonColor.y = arr[1].get<float>();
                Config::skeletonColor.z = arr[2].get<float>();
                Config::skeletonColor.w = arr[3].get<float>();
            }

            if (j.contains("espColor") && j["espColor"].is_array() && j["espColor"].size() == 4)
            {
                auto arr = j["espColor"];
                Config::espColor.x = arr[0].get<float>();
                Config::espColor.y = arr[1].get<float>();
                Config::espColor.z = arr[2].get<float>();
                Config::espColor.w = arr[3].get<float>();
            }

            if (j.contains("espColorT") && j["espColorT"].is_array() && j["espColorT"].size() == 4)
            {
                auto arr = j["espColorT"];
                Config::espColorT.x = arr[0].get<float>();
                Config::espColorT.y = arr[1].get<float>();
                Config::espColorT.z = arr[2].get<float>();
                Config::espColorT.w = arr[3].get<float>();
            }

            if (j.contains("espColorCT") && j["espColorCT"].is_array() && j["espColorCT"].size() == 4)
            {
                auto arr = j["espColorCT"];
                Config::espColorCT.x = arr[0].get<float>();
                Config::espColorCT.y = arr[1].get<float>();
                Config::espColorCT.z = arr[2].get<float>();
                Config::espColorCT.w = arr[3].get<float>();
            }

            Config::Night = j.value("Night", false);
            if (j.contains("NightColor") && j["NightColor"].is_array() && j["NightColor"].size() == 4)
            {
                auto arr = j["NightColor"];
                Config::NightColor.x = arr[0].get<float>();
                Config::NightColor.y = arr[1].get<float>();
                Config::NightColor.z = arr[2].get<float>();
                Config::NightColor.w = arr[3].get<float>();
            }

            Config::enemyChamsInvisible = j.value("enemyChamsInvisible", false);
            Config::enemyChams = j.value("enemyChams", false);
            Config::teamChams = j.value("teamChams", false);
            Config::teamChamsInvisible = j.value("teamChamsInvisible", false);
            // Clamped here too: the value indexes a fixed 3-entry material table.
            Config::chamsMaterial = std::clamp(j.value("chamsMaterial", 0), 0, 2);

            Config::fov_circle = j.value("fov_circle", false);
            Config::aimbot = j.value("aimbot", false);
            Config::aimbot_fov = j.value("aimbot_fov", 0.f);
            Config::team_check = j.value("team_check", j.value("teamCheckAim", false));

            // VK_XBUTTON1 (mouse 4) remains the compatibility default for
            // older configs that predate keybind persistence.
            const int aimbotKey = j.value("aimbotKey", 5);
            keybind.setKey(Config::aimbot, aimbotKey);

            Config::antiflash = j.value("antiflash", false);
            Config::noSmoke = j.value("noSmoke", false);

            Config::armChams = j.value("armChams", false);
            Config::viewmodelChams = j.value("viewmodelChams", false);
            Config::armChamsMaterial = std::clamp(j.value("armChamsMaterial", 0), 0, 2);
            Config::viewmodelChamsMaterial = std::clamp(j.value("viewmodelChamsMaterial", 0), 0, 2);

            if (j.contains("colArmChams") && j["colArmChams"].is_array() && j["colArmChams"].size() == 4)
            {
                auto arr = j["colArmChams"];
                Config::colArmChams.x = arr[0].get<float>();
                Config::colArmChams.y = arr[1].get<float>();
                Config::colArmChams.z = arr[2].get<float>();
                Config::colArmChams.w = arr[3].get<float>();
            }

            if (j.contains("colViewmodelChams") && j["colViewmodelChams"].is_array() && j["colViewmodelChams"].size() == 4)
            {
                auto arr = j["colViewmodelChams"];
                Config::colViewmodelChams.x = arr[0].get<float>();
                Config::colViewmodelChams.y = arr[1].get<float>();
                Config::colViewmodelChams.z = arr[2].get<float>();
                Config::colViewmodelChams.w = arr[3].get<float>();
            }

            if (j.contains("colVisualChams") && j["colVisualChams"].is_array() && j["colVisualChams"].size() == 4)
            {
                auto arr = j["colVisualChams"];
                Config::colVisualChams.x = arr[0].get<float>();
                Config::colVisualChams.y = arr[1].get<float>();
                Config::colVisualChams.z = arr[2].get<float>();
                Config::colVisualChams.w = arr[3].get<float>();
            }

            if (j.contains("colVisualChamsIgnoreZ") && j["colVisualChamsIgnoreZ"].is_array() && j["colVisualChamsIgnoreZ"].size() == 4)
            {
                auto arr = j["colVisualChamsIgnoreZ"];
                Config::colVisualChamsIgnoreZ.x = arr[0].get<float>();
                Config::colVisualChamsIgnoreZ.y = arr[1].get<float>();
                Config::colVisualChamsIgnoreZ.z = arr[2].get<float>();
                Config::colVisualChamsIgnoreZ.w = arr[3].get<float>();
            }

            if (j.contains("teamcolVisualChamsIgnoreZ") && j["teamcolVisualChamsIgnoreZ"].is_array() && j["teamcolVisualChamsIgnoreZ"].size() == 4)
            {
                auto arr = j["teamcolVisualChamsIgnoreZ"];
                Config::teamcolVisualChamsIgnoreZ.x = arr[0].get<float>();
                Config::teamcolVisualChamsIgnoreZ.y = arr[1].get<float>();
                Config::teamcolVisualChamsIgnoreZ.z = arr[2].get<float>();
                Config::teamcolVisualChamsIgnoreZ.w = arr[3].get<float>();
            }

            if (j.contains("teamcolVisualChams") && j["teamcolVisualChams"].is_array() && j["teamcolVisualChams"].size() == 4)
            {
                auto arr = j["teamcolVisualChams"];
                Config::teamcolVisualChams.x = arr[0].get<float>();
                Config::teamcolVisualChams.y = arr[1].get<float>();
                Config::teamcolVisualChams.z = arr[2].get<float>();
                Config::teamcolVisualChams.w = arr[3].get<float>();
            }

            if (j.contains("fovCircleColor") && j["fovCircleColor"].is_array() && j["fovCircleColor"].size() == 4) {
                auto arr = j["fovCircleColor"];
                Config::fovCircleColor.x = arr[0].get<float>();
                Config::fovCircleColor.y = arr[1].get<float>();
                Config::fovCircleColor.z = arr[2].get<float>();
                Config::fovCircleColor.w = arr[3].get<float>();
            }

            ifs.close();
        }

        static void Remove(const std::string& configName)
        {
            auto filePath = GetConfigPath(configName);
            if (std::filesystem::exists(filePath))
            {
                std::error_code ec;
                std::filesystem::remove(filePath, ec);
            }
        }
    };
}
