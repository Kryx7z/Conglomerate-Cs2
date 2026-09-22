#include "menu.h"
#include "../config/config.h"
#include "../interfaces/interfaces.h"
#include "../utils/memory/vfunc/vfunc.h"
#include "../utils/memory/safe_memory.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "../config/configmanager.h"

#include "../keybinds/keybinds.h"


void ApplyImGuiTheme() {
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImVec4 primaryColor = ImVec4(0.44f, 0.23f, 0.78f, 1.0f);
    ImVec4 outlineColor = ImVec4(0.54f, 0.33f, 0.88f, 0.7f);

    colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.13f, 1.0f);
    colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.11f, 0.13f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.15f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.15f, 0.18f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.11f, 0.13f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.11f, 0.13f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.11f, 0.13f, 1.0f);

    colors[ImGuiCol_Button] = primaryColor;
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.54f, 0.33f, 0.88f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.34f, 0.13f, 0.68f, 1.0f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.50f, 1.00f, 1.0f);
    colors[ImGuiCol_SliderGrab] = primaryColor;
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.54f, 0.33f, 0.88f, 1.0f);

    colors[ImGuiCol_Header] = primaryColor;
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.54f, 0.33f, 0.88f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.13f, 0.68f, 1.0f);

    colors[ImGuiCol_Separator] = ImVec4(0.34f, 0.13f, 0.68f, 1.0f);
    colors[ImGuiCol_SeparatorHovered] = primaryColor;
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.54f, 0.33f, 0.88f, 1.0f);

    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.17f, 0.17f, 0.21f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.44f, 0.23f, 0.78f, 0.8f);
    colors[ImGuiCol_TabActive] = primaryColor;
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.17f, 0.17f, 0.21f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.34f, 0.13f, 0.68f, 1.0f);

    colors[ImGuiCol_Border] = outlineColor;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    style.WindowRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.PopupRounding = 0.0f;

    style.ItemSpacing = ImVec2(8, 4);
    style.FramePadding = ImVec2(4, 3);
    style.WindowPadding = ImVec2(8, 8);

    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.WindowBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;

    style.GrabMinSize = 7.0f;
}

struct ColorHexEditState
{
    ImGuiID id = 0;
    char text[16] = {};
    bool editing = false;
};

static ColorHexEditState colorHexEditStates[16] = {};

static ColorHexEditState& GetColorHexEditState()
{
    const ImGuiID id = ImGui::GetID("##Hex");

    for (auto& state : colorHexEditStates)
    {
        if (state.id == id)
            return state;
    }

    for (auto& state : colorHexEditStates)
    {
        if (state.id == 0)
        {
            state.id = id;
            return state;
        }
    }

    return colorHexEditStates[0];
}

static int HexDigitValue(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

static bool ParseHexColor(const char* text, ImVec4& color)
{
    char digits[8] = {};
    int digitCount = 0;

    for (const char* current = text; *current != '\0'; ++current)
    {
        if (*current == '#' || *current == ' ' || *current == '\t')
            continue;

        if (HexDigitValue(*current) < 0 || digitCount >= IM_ARRAYSIZE(digits))
            return false;

        digits[digitCount++] = *current;
    }

    if (digitCount != 6 && digitCount != 8)
        return false;

    auto readComponent = [&digits](int index) -> float
    {
        const int high = HexDigitValue(digits[index]);
        const int low = HexDigitValue(digits[index + 1]);
        return static_cast<float>((high << 4) | low) / 255.0f;
    };

    color.x = readComponent(0);
    color.y = readComponent(2);
    color.z = readComponent(4);
    if (digitCount == 8)
        color.w = readComponent(6);
    else
        color.w = 1.0f;

    return true;
}

static int ColorByte(float component)
{
    if (component < 0.0f)
        component = 0.0f;
    else if (component > 1.0f)
        component = 1.0f;

    return static_cast<int>(component * 255.0f + 0.5f);
}

static void FormatHexColor(char* output, size_t outputSize, const ImVec4& color)
{
    const int red = ColorByte(color.x);
    const int green = ColorByte(color.y);
    const int blue = ColorByte(color.z);
    const int alpha = ColorByte(color.w);

    std::snprintf(output, outputSize, "#%02X%02X%02X%02X", red, green, blue, alpha);
}

static bool ColorEditHexOnly(const char* label, ImVec4& color)
{
    bool valueChanged = false;

    ImGui::PushID(label);
    if (ImGui::ColorButton(
            "##ColorPreview",
            color,
            ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoTooltip,
            ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
    {
        ImGui::OpenPopup("##ColorPicker");
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    if (ImGui::BeginPopup("##ColorPicker"))
    {
        valueChanged |= ImGui::ColorPicker4(
            "##Picker",
            &color.x,
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_InputRGB |
            ImGuiColorEditFlags_PickerHueBar |
            ImGuiColorEditFlags_NoSidePreview |
            ImGuiColorEditFlags_NoOptions |
            ImGuiColorEditFlags_NoLabel);

        ColorHexEditState& hexState = GetColorHexEditState();
        if (!hexState.editing)
            FormatHexColor(hexState.text, IM_ARRAYSIZE(hexState.text), color);

        ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * 12.0f);
        if (ImGui::InputText(
                "##Hex",
                hexState.text,
                IM_ARRAYSIZE(hexState.text),
                ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase))
        {
            ImVec4 parsedColor = color;
            if (ParseHexColor(hexState.text, parsedColor))
            {
                color = parsedColor;
                valueChanged = true;
            }
        }

        hexState.editing = ImGui::IsItemActive();
        ImGui::EndPopup();
    }
    else
    {
        GetColorHexEditState().editing = false;
    }

    ImGui::PopID();
    return valueChanged;
}

Menu::Menu() {
    activeTab = 0;
    showMenu = false;
    UiState::menuOpen = false;
}

void Menu::init(HWND& window, ID3D11Device* pDevice, ID3D11DeviceContext* pContext, ID3D11RenderTargetView* mainRenderTargetView) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags = 0;
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(pDevice, pContext);

    ApplyImGuiTheme();

    if (io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f) == nullptr)
    {
        io.Fonts->AddFontDefault();
    }

}

void Menu::render() {
    keybind.pollInputs();
    if (showMenu) {
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar;

        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Once);
        ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_Once);

        ImGui::Begin("Conglomerate", nullptr, window_flags);

        {
            float windowWidth = ImGui::GetWindowWidth();
            float rightTextWidth = ImGui::CalcTextSize("kryx.lol").x;

            ImGui::Text("Conglomerate");

            ImGui::SameLine(windowWidth - rightTextWidth - 10);
            ImGui::Text("kryx.lol");
        }

        ImGui::Separator();

        const char* tabNames[] = { "Aim", "Visuals", "Movement", "Misc", "Config" };

        if (ImGui::BeginTabBar("MainTabBar", ImGuiTabBarFlags_NoTooltip)) {
            for (int i = 0; i < 5; i++) {
                if (ImGui::BeginTabItem(tabNames[i])) {
                    activeTab = i;
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::BeginChild("ContentRegion", ImVec2(0, 0), false);

        switch (activeTab) {
        case 0:
        {
            ImGui::BeginChild("AimLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 5, 0), true);
            ImGui::Text("General");
            ImGui::Separator();

            ImGui::Checkbox("Enable##AimBot", &Config::aimbot);
            ImGui::SameLine();
            ImGui::Text("Key:");
            ImGui::SameLine();
            keybind.menuButton(Config::aimbot);

            ImGui::Checkbox("Team Check", &Config::team_check);
            ImGui::SliderFloat("FOV", &Config::aimbot_fov, 0.f, 90.f);
            ImGui::Checkbox("Draw FOV Circle", &Config::fov_circle);
            if (Config::fov_circle) {
                ColorEditHexOnly("Circle Color", Config::fovCircleColor);
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("AimRight", ImVec2(0, 0), true);
            ImGui::Text("TriggerBot");
            ImGui::Separator();
            ImGui::Text("No additional settings");

            ImGui::EndChild();
        }
        break;

        case 1:
        {
            ImGui::BeginChild("VisualsLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 5, 0), true);
            ImGui::Text("Player ESP");
            ImGui::Separator();

            ImGui::Checkbox("Box", &Config::esp);
            ImGui::SliderFloat("Thickness", &Config::espThickness, 1.0f, 5.0f);
            ImGui::Checkbox("Box Fill", &Config::espFill);
            if (Config::espFill) {
                ImGui::SliderFloat("Fill Opacity", &Config::espFillOpacity, 0.0f, 1.0f);
            }
            ColorEditHexOnly("T Color", Config::espColorT);
            ColorEditHexOnly("CT Color", Config::espColorCT);
            ImGui::Checkbox("Team Check", &Config::teamCheck);
            ImGui::Checkbox("Health Bar", &Config::showHealth);
            ImGui::Checkbox("Name Tags", &Config::showNameTags);
            ImGui::Checkbox("Flags", &Config::flags);
            ImGui::Checkbox("Skeleton ESP", &Config::skeleton);
            if (Config::skeleton)
            {
                ImGui::SliderFloat("Skeleton Thickness", &Config::skeletonThickness, 1.0f, 5.0f);
                ColorEditHexOnly("Skeleton Color", Config::skeletonColor);
            }

            ImGui::Spacing();
            ImGui::Text("World");
            ImGui::Separator();

            ImGui::Checkbox("Night Mode", &Config::Night);
            if (Config::Night) {
                ColorEditHexOnly("Night Color", Config::NightColor);
            }

            ImGui::Checkbox("Custom FOV", &Config::fovEnabled);
            if (Config::fovEnabled) {
                ImGui::SliderFloat("FOV Value##FovSlider", &Config::fov, 20.0f, 160.0f, "%1.0f");
            }

            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("VisualsRight", ImVec2(0, 0), true);
            ImGui::Text("Chams");
            ImGui::Separator();

            ImGui::Checkbox("Visible Chams##ChamsCheckbox", &Config::enemyChams);
            const char* chamsMaterials[] = { "Flat", "Illuminate", "Glow" };
            ImGui::Combo("Material##VisibleChamsMaterial", &Config::chamsMaterial, chamsMaterials, IM_ARRAYSIZE(chamsMaterials));
            if (Config::enemyChams) {
                ColorEditHexOnly("Visible Chams Color", Config::colVisualChams);
            }
            ImGui::Checkbox("Occluded Chams", &Config::enemyChamsInvisible);
            if (Config::enemyChamsInvisible) {
                ColorEditHexOnly("Occluded Chams Color", Config::colVisualChamsIgnoreZ);
            }

            ImGui::Spacing();
            ImGui::Text("Model Chams");
            ImGui::Separator();

            ImGui::Checkbox("Viewmodel Chams", &Config::armChams);
            if (Config::armChams) {
                ImGui::Combo("Material##ViewmodelChamsMaterial", &Config::armChamsMaterial, chamsMaterials, IM_ARRAYSIZE(chamsMaterials));
                ColorEditHexOnly("Viewmodel Color", Config::colArmChams);
            }
            ImGui::Checkbox("Gun Chams", &Config::viewmodelChams);
            if (Config::viewmodelChams) {
                ImGui::Combo("Material##GunChamsMaterial", &Config::viewmodelChamsMaterial, chamsMaterials, IM_ARRAYSIZE(chamsMaterials));
                ColorEditHexOnly("Gun Color", Config::colViewmodelChams);
            }

            ImGui::Spacing();
            ImGui::Text("Removals");
            ImGui::Separator();

            ImGui::Checkbox("Anti Flash", &Config::antiflash);
            ImGui::Checkbox("Remove Smoke", &Config::noSmoke);

            ImGui::EndChild();
        }
        break;

        case 2:
        {
            ImGui::BeginChild("MovementLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 5, 0), true);
            ImGui::Text("Movement");
            ImGui::Separator();

            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("MovementRight", ImVec2(0, 0), true);
            ImGui::Text("Options");
            ImGui::Separator();

            ImGui::EndChild();
        }
        break;

        case 3:
        {
            ImGui::BeginChild("Misc", ImVec2(0, 0), true);
            ImGui::Text("Miscellaneous");
            ImGui::Separator();

            ImGui::Checkbox("Watermark", &Config::watermark);
            ImGui::Checkbox("Spectator List", &Config::spectatorList);

            ImGui::EndChild();
        }
        break;

        case 4:
        {
            ImGui::BeginChild("ConfigLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 5, 0), true);
            ImGui::Text("General");
            ImGui::Separator();

            static char configName[128] = "";
            static std::vector<std::string> configList = internal_config::ConfigManager::ListConfigs();
            static int selectedConfigIndex = -1;

            ImGui::InputText("Config Name", configName, IM_ARRAYSIZE(configName));

            if (ImGui::Button("Refresh")) {
                configList = internal_config::ConfigManager::ListConfigs();
            }
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                internal_config::ConfigManager::Load(configName);
            }
            ImGui::SameLine();
            if (ImGui::Button("Save")) {
                internal_config::ConfigManager::Save(configName);
                configList = internal_config::ConfigManager::ListConfigs();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                internal_config::ConfigManager::Remove(configName);
                configList = internal_config::ConfigManager::ListConfigs();
            }

            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("ConfigRight", ImVec2(0, 0), true);
            ImGui::Text("Saved Configs");
            ImGui::Separator();

            for (int i = 0; i < static_cast<int>(configList.size()); i++) {
                if (ImGui::Selectable(configList[i].c_str(), selectedConfigIndex == i)) {
                    selectedConfigIndex = i;
                    strncpy_s(configName, sizeof(configName), configList[i].c_str(), _TRUNCATE);
                }
            }

            ImGui::EndChild();
        }
        break;
        }

        ImGui::EndChild();
        ImGui::End();
    }
}

void Menu::toggleMenu() {
    static int cursorShowAdjustments = 0;

    showMenu = !showMenu;
    UiState::menuOpen = showMenu;

    if (showMenu) {

        if (I::InputSystem) {
            std::uint8_t relativeMouse = 0;
            const auto inputSystemAddress = reinterpret_cast<std::uintptr_t>(I::InputSystem);
            if (SafeMemory::read(inputSystemAddress + 84u, relativeMouse))
            {
                savedRelativeMouse = relativeMouse != 0;
                relativeMouseStateCaptured = true;
                __try
                {
                    M::vfunc<void, 76U>(I::InputSystem, false);
                }
                __except (SehDiagnostics::handle("menu.input.capture"))
                {
                    relativeMouseStateCaptured = false;
                }
            }
        }

        ClipCursor(nullptr);
        do {
            ++cursorShowAdjustments;
        } while (ShowCursor(TRUE) < 0);

        ImGui::GetIO().MouseDrawCursor = true;
    }
    else {
        if (I::InputSystem && relativeMouseStateCaptured) {
            __try
            {
                M::vfunc<void, 76U>(I::InputSystem, savedRelativeMouse);
            }
            __except (SehDiagnostics::handle("menu.input.restore"))
            {
            }
        }
        relativeMouseStateCaptured = false;

        ClipCursor(nullptr);
        while (cursorShowAdjustments > 0) {
            ShowCursor(FALSE);
            --cursorShowAdjustments;
        }

        ImGui::GetIO().MouseDrawCursor = false;
    }
}
