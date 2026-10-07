#include "menu.h"
#include "../config/config.h"
#include "../interfaces/interfaces.h"
#include "../utils/debug_console.h"
#include "../utils/memory/vfunc/vfunc.h"
#include "../utils/memory/safe_memory.h"

#include <Windows.h>
#include <wincodec.h>
#include <objbase.h>
#include <cstdint>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <memory>
#include <limits>
#include <string>
#include <vector>
#include "../config/configmanager.h"

#include "../keybinds/keybinds.h"

extern HMODULE g_moduleInstance;

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
	constexpr WORD kPreviewModelResourceId = 101;
	ID3D11ShaderResourceView* g_previewModelTexture = nullptr;
	ImVec2 g_previewUvMin(0.0f, 0.0f);
	ImVec2 g_previewUvMax(1.0f, 1.0f);
	float g_previewModelAspect = 0.35f;
	bool g_previewCounterTerrorist = false;

	template <typename T>
	struct ComRelease
	{
		void operator()(T* value) const
		{
			if (value)
				value->Release();
		}
	};

	template <typename T>
	using ComPtr = std::unique_ptr<T, ComRelease<T>>;

	bool LoadPreviewModel(ID3D11Device* device)
	{
		if (!device || g_previewModelTexture)
			return g_previewModelTexture != nullptr;

		const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool uninitializeCom = SUCCEEDED(comResult);
		if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
		{
			DebugConsole::logf("[menu] ESP preview load failed at COM initialization: hr=0x%08X",
				static_cast<unsigned int>(comResult));
			return false;
		}

		bool loaded = false;
		const char* failedAt = "module lookup";
		HMODULE module = g_moduleInstance;
		if (module)
		{
			failedAt = "resource lookup";
			const HRSRC resource = FindResourceW(module,
				MAKEINTRESOURCEW(kPreviewModelResourceId), MAKEINTRESOURCEW(10));
			const DWORD resourceSize = resource ? SizeofResource(module, resource) : 0;
			const HGLOBAL resourceData = resource ? LoadResource(module, resource) : nullptr;
			auto* pngData = resourceData ? static_cast<BYTE*>(LockResource(resourceData)) : nullptr;

			IWICImagingFactory* rawFactory = nullptr;
			failedAt = "WIC imaging factory/resource lock";
			if (pngData && resourceSize > 0 && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,
				nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rawFactory))))
			{
				ComPtr<IWICImagingFactory> factory(rawFactory);
				IWICStream* rawStream = nullptr;
			IWICBitmapDecoder* rawDecoder = nullptr;
			IWICBitmapFrameDecode* rawFrame = nullptr;
			IWICFormatConverter* rawConverter = nullptr;

			failedAt = "WIC stream creation";
			if (SUCCEEDED(factory->CreateStream(&rawStream)))
			{
				ComPtr<IWICStream> stream(rawStream);
				failedAt = "WIC stream initialization/PNG decode";
				if (SUCCEEDED(stream->InitializeFromMemory(pngData, resourceSize)) &&
						SUCCEEDED(factory->CreateDecoderFromStream(stream.get(), nullptr,
							WICDecodeMetadataCacheOnLoad, &rawDecoder)))
					{
						ComPtr<IWICBitmapDecoder> decoder(rawDecoder);
						failedAt = "WIC frame retrieval";
						if (SUCCEEDED(decoder->GetFrame(0, &rawFrame)))
						{
							ComPtr<IWICBitmapFrameDecode> frame(rawFrame);
							failedAt = "WIC converter creation";
							if (SUCCEEDED(factory->CreateFormatConverter(&rawConverter)))
							{
								ComPtr<IWICFormatConverter> converter(rawConverter);
								failedAt = "WIC RGBA conversion";
								if (SUCCEEDED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA,
									WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
								{
									UINT width = 0;
									UINT height = 0;
									failedAt = "WIC image size/copy";
									if (SUCCEEDED(converter->GetSize(&width, &height)) && width && height &&
										width <= (std::numeric_limits<UINT>::max)() / 4u &&
										height <= (std::numeric_limits<UINT>::max)() / (width * 4u))
									{
										const UINT stride = width * 4u;
										const UINT bufferSize = stride * height;
										std::vector<BYTE> pixels(bufferSize);
											if (SUCCEEDED(converter->CopyPixels(nullptr, stride, bufferSize, pixels.data())))
										{
											failedAt = "transparent image scan";
											UINT minX = width, minY = height, maxX = 0, maxY = 0;
											bool hasVisiblePixel = false;
											for (UINT y = 0; y < height; ++y)
											{
												for (UINT x = 0; x < width; ++x)
												{
													BYTE& alpha = pixels[static_cast<std::size_t>(y) * stride + x * 4u + 3u];
													if (alpha < 8u)
														alpha = 0;
													if (alpha < 32u)
														continue;
													hasVisiblePixel = true;
													minX = (std::min)(minX, x);
													minY = (std::min)(minY, y);
													maxX = (std::max)(maxX, x);
													maxY = (std::max)(maxY, y);
												}
											}

											if (hasVisiblePixel)
											{
												failedAt = "D3D11 texture creation";
												g_previewUvMin = ImVec2(static_cast<float>(minX) / width,
													static_cast<float>(minY) / height);
												g_previewUvMax = ImVec2(static_cast<float>(maxX + 1u) / width,
													static_cast<float>(maxY + 1u) / height);
												g_previewModelAspect = static_cast<float>(maxX - minX + 1u) /
													static_cast<float>(maxY - minY + 1u);

												D3D11_TEXTURE2D_DESC textureDesc{};
												textureDesc.Width = width;
												textureDesc.Height = height;
												textureDesc.MipLevels = 1;
												textureDesc.ArraySize = 1;
												textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
												textureDesc.SampleDesc.Count = 1;
												textureDesc.Usage = D3D11_USAGE_DEFAULT;
												textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

												D3D11_SUBRESOURCE_DATA textureData{};
												textureData.pSysMem = pixels.data();
												textureData.SysMemPitch = stride;
												ID3D11Texture2D* rawTexture = nullptr;
												if (SUCCEEDED(device->CreateTexture2D(&textureDesc, &textureData, &rawTexture)))
												{
													ComPtr<ID3D11Texture2D> texture(rawTexture);
													failedAt = "D3D11 shader-view creation";
													loaded = SUCCEEDED(device->CreateShaderResourceView(texture.get(), nullptr,
														&g_previewModelTexture));
												}
											}
										}
								}
							}
						}
					}
				}
			}
		}
		}

		if (!loaded)
			DebugConsole::logf("[menu] ESP preview model texture could not be loaded; stage=%s",
				failedAt);
		if (uninitializeCom)
			CoUninitialize();
		return loaded;
	}
}


void ApplyImGuiTheme() {
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 purple(168.0f / 255.0f, 155.0f / 255.0f, 242.0f / 255.0f, 1.0f); // #A89BF2
    const ImVec4 purpleBright(190.0f / 255.0f, 181.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    const ImVec4 black(22.0f / 255.0f, 21.0f / 255.0f, 27.0f / 255.0f, 1.0f); // Cynth #16151B
    const ImVec4 surface(23.0f / 255.0f, 22.0f / 255.0f, 28.0f / 255.0f, 1.0f); // #17161C
    const ImVec4 surfaceHover(0.15f, 0.14f, 0.19f, 1.0f);
    const ImVec4 outline(0.16f, 0.15f, 0.19f, 1.0f);
    const ImVec4 muted(0.62f, 0.58f, 0.79f, 1.0f);

    colors[ImGuiCol_WindowBg] = black;
    colors[ImGuiCol_ChildBg] = surface;
    colors[ImGuiCol_PopupBg] = black;
    colors[ImGuiCol_Border] = outline;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
    colors[ImGuiCol_Text] = purple;
    colors[ImGuiCol_TextDisabled] = muted;
    colors[ImGuiCol_FrameBg] = black;
    colors[ImGuiCol_FrameBgHovered] = surfaceHover;
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.14f, 0.23f, 1.0f);
    colors[ImGuiCol_CheckMark] = black;
    colors[ImGuiCol_SliderGrab] = purple;
    colors[ImGuiCol_SliderGrabActive] = purpleBright;
    colors[ImGuiCol_Button] = surface;
    colors[ImGuiCol_ButtonHovered] = surfaceHover;
    colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.14f, 0.23f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.15f, 0.13f, 0.24f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = surfaceHover;
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.17f, 0.32f, 1.0f);
    colors[ImGuiCol_Separator] = outline;
    colors[ImGuiCol_SeparatorHovered] = purple;
    colors[ImGuiCol_SeparatorActive] = purpleBright;
    colors[ImGuiCol_Tab] = surface;
    colors[ImGuiCol_TabHovered] = surfaceHover;
    colors[ImGuiCol_TabActive] = ImVec4(0.15f, 0.13f, 0.24f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = surface;
    colors[ImGuiCol_TabUnfocusedActive] = surfaceHover;

    style.WindowRounding = 13.0f;
    style.FrameRounding = 6.0f;
    style.ScrollbarRounding = 7.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 7.0f;
    style.ChildRounding = 10.0f;
    style.PopupRounding = 8.0f;

    style.ItemSpacing = ImVec2(10.0f, 7.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.GrabMinSize = 9.0f;
}

static const ImVec4 kMenuPurple(168.0f / 255.0f, 155.0f / 255.0f, 242.0f / 255.0f, 1.0f);
static constexpr ImU32 kMenuPurpleU32 = IM_COL32(168, 155, 242, 255);
static constexpr ImU32 kMenuCheckU32 = IM_COL32(22, 21, 27, 255);

static bool MenuCheckbox(const char* label, bool* value)
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(14.0f / 255.0f, 13.0f / 255.0f, 17.0f / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(28.0f / 255.0f, 26.0f / 255.0f, 34.0f / 255.0f, 1.0f));
    const bool changed = ImGui::Checkbox(label, value);
    ImGui::PopStyleColor(2);
    if (*value)
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const float boxSize = ImGui::GetFrameHeight();
        const float rounding = ImGui::GetStyle().FrameRounding;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddRectFilled(min, ImVec2(min.x + boxSize, min.y + boxSize), kMenuPurpleU32, rounding);
        drawList->AddLine(ImVec2(min.x + boxSize * 0.24f, min.y + boxSize * 0.52f),
                          ImVec2(min.x + boxSize * 0.43f, min.y + boxSize * 0.70f), kMenuCheckU32, 2.0f);
        drawList->AddLine(ImVec2(min.x + boxSize * 0.43f, min.y + boxSize * 0.70f),
                          ImVec2(min.x + boxSize * 0.78f, min.y + boxSize * 0.30f), kMenuCheckU32, 2.0f);
    }
    return changed;
}

static bool MenuSliderFloat(const char* label, float* value, float min, float max, const char* format = "%.3f")
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(14.0f / 255.0f, 13.0f / 255.0f, 17.0f / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(28.0f / 255.0f, 26.0f / 255.0f, 34.0f / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(38.0f / 255.0f, 35.0f / 255.0f, 45.0f / 255.0f, 1.0f));
    const bool changed = ImGui::SliderFloat(label, value, min, max, format);
    ImGui::PopStyleColor(3);
    return changed;
}

static bool MenuSliderInt(const char* label, int* value, int min, int max, const char* format = "%d")
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(14.0f / 255.0f, 13.0f / 255.0f, 17.0f / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(28.0f / 255.0f, 26.0f / 255.0f, 34.0f / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(38.0f / 255.0f, 35.0f / 255.0f, 45.0f / 255.0f, 1.0f));
    const bool changed = ImGui::SliderInt(label, value, min, max, format);
    ImGui::PopStyleColor(3);
    return changed;
}

static void BeginMenuPanel(const char* id, const char* title, const ImVec2& size)
{
    const ImVec2 panelPos = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 panelSize(size.x > 0.0f ? size.x : available.x, size.y > 0.0f ? size.y : available.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        ImVec2(panelPos.x + 1.0f, panelPos.y + 6.0f),
        ImVec2(panelPos.x + panelSize.x + 1.0f, panelPos.y + panelSize.y + 6.0f),
        IM_COL32(0, 0, 0, 145), 10.0f);
    drawList->AddRectFilled(
        ImVec2(panelPos.x + 1.0f, panelPos.y + 3.0f),
        ImVec2(panelPos.x + panelSize.x + 1.0f, panelPos.y + panelSize.y + 3.0f),
        IM_COL32(0, 0, 0, 55), 10.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(23.0f / 255.0f, 22.0f / 255.0f, 28.0f / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.16f, 0.15f, 0.19f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::BeginChild(id, size, true);
    ImGui::TextColored(kMenuPurple, "%s", title);
    ImGui::Separator();
    ImGui::Spacing();
}

static void EndMenuPanel()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

static void RenderPreviewPanel(const ImVec2& size)
{
    BeginMenuPanel("PreviewPanel", "Preview", size);

    const float tabWidth = 39.0f;
    const float tabsWidth = tabWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - tabsWidth) * 0.5f);
    const auto drawTeamTab = [&](const char* label, bool isCounterTerrorist)
    {
        const bool selected = g_previewCounterTerrorist == isCounterTerrorist;
        ImGui::PushStyleColor(ImGuiCol_Button, selected
            ? ImVec4(0.23f, 0.20f, 0.36f, 1.0f)
            : ImVec4(22.0f / 255.0f, 21.0f / 255.0f, 27.0f / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.14f, 0.19f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.17f, 0.32f, 1.0f));
        if (ImGui::Button(label, ImVec2(tabWidth, 26.0f)))
            g_previewCounterTerrorist = isCounterTerrorist;
        ImGui::PopStyleColor(3);
    };
    drawTeamTab("T", false);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
    drawTeamTab("CT", true);

    ImGui::Spacing();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x <= 1.0f || canvasSize.y <= 1.0f)
    {
        EndMenuPanel();
        return;
    }

    ImGui::InvisibleButton("##EspPreviewCanvas", canvasSize);
    const ImVec2 canvasMin = ImGui::GetItemRectMin();
    const ImVec2 canvasMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(canvasMin, canvasMax, true);

    if (!g_previewModelTexture)
    {
        const char* message = "Model preview unavailable";
        const ImVec2 messageSize = ImGui::CalcTextSize(message);
        drawList->AddText(ImVec2(canvasMin.x + (canvasSize.x - messageSize.x) * 0.5f,
            canvasMin.y + (canvasSize.y - messageSize.y) * 0.5f),
            IM_COL32(168, 155, 242, 255), message);
        drawList->PopClipRect();
        EndMenuPanel();
        return;
    }

    const bool espPreviewEnabled = Config::esp;
    const bool showFlags = espPreviewEnabled && Config::flags;
    const bool showName = espPreviewEnabled && Config::showNameTags;
    const float flagColumnWidth = showFlags ? 52.0f : 0.0f;
    const float flagGap = showFlags ? 8.0f : 0.0f;
    const float nameSpace = (showName ? 21.0f : 5.0f) + (showFlags ? 13.0f : 0.0f);
    const float weaponSpace = showFlags ? 23.0f : 5.0f;
    const float modelAreaWidth = (std::max)(1.0f, canvasSize.x - flagColumnWidth - flagGap);
    const float modelAreaHeight = (std::max)(1.0f, canvasSize.y - nameSpace - weaponSpace);
    const float imageHeight = (std::min)(modelAreaHeight, modelAreaWidth / g_previewModelAspect);
    const float imageWidth = imageHeight * g_previewModelAspect;
    const ImVec2 imageMin(canvasMin.x + (modelAreaWidth - imageWidth) * 0.5f,
        canvasMin.y + nameSpace + (modelAreaHeight - imageHeight) * 0.5f);
    const ImVec2 imageMax(imageMin.x + imageWidth, imageMin.y + imageHeight);
    const ImVec2 boxMin(imageMin.x - 5.0f, imageMin.y - 2.0f);
    const ImVec2 boxMax(imageMax.x + 5.0f, imageMax.y + 2.0f);

    const ImVec4 teamColor = g_previewCounterTerrorist ? Config::espColorCT : Config::espColorT;
    const ImU32 boxColor = ImGui::ColorConvertFloat4ToU32(teamColor);
    if (espPreviewEnabled && Config::espFill)
    {
        ImVec4 fillColor = teamColor;
        fillColor.w = Config::espFillOpacity;
        drawList->AddRectFilled(boxMin, boxMax, ImGui::ColorConvertFloat4ToU32(fillColor));
    }

    drawList->AddImage(reinterpret_cast<ImTextureID>(g_previewModelTexture), imageMin, imageMax,
        g_previewUvMin, g_previewUvMax);

    if (Config::esp)
        drawList->AddRect(boxMin, boxMax, boxColor, 0.0f, 0, Config::espThickness);

    if (espPreviewEnabled && Config::skeleton)
    {
        const ImU32 skeletonColor = ImGui::ColorConvertFloat4ToU32(Config::skeletonColor);
        const auto point = [&](float x, float y)
        {
            return ImVec2(imageMin.x + imageWidth * x, imageMin.y + imageHeight * y);
        };
        const auto bone = [&](float x1, float y1, float x2, float y2)
        {
            drawList->AddLine(point(x1, y1), point(x2, y2), skeletonColor, Config::skeletonThickness);
        };

        // ts doesnt work bruh
        bone(0.52f, 0.19f, 0.35f, 0.23f);
        bone(0.52f, 0.19f, 0.68f, 0.23f);
        bone(0.52f, 0.19f, 0.52f, 0.40f);
        bone(0.35f, 0.23f, 0.23f, 0.39f);
        bone(0.23f, 0.39f, 0.15f, 0.48f);
        bone(0.68f, 0.23f, 0.82f, 0.37f);
        bone(0.82f, 0.37f, 0.73f, 0.24f);
        bone(0.52f, 0.40f, 0.52f, 0.59f);
        bone(0.52f, 0.59f, 0.36f, 0.62f);
        bone(0.52f, 0.59f, 0.68f, 0.62f);
        bone(0.36f, 0.62f, 0.39f, 0.79f);
        bone(0.39f, 0.79f, 0.38f, 0.95f);
        bone(0.38f, 0.95f, 0.31f, 0.99f);
        bone(0.68f, 0.62f, 0.65f, 0.79f);
        bone(0.65f, 0.79f, 0.68f, 0.95f);
        bone(0.68f, 0.95f, 0.76f, 0.99f);
    }

    if (espPreviewEnabled && Config::showHealth)
    {
        constexpr float previewHealth = 74.0f;
        const float barWidth = 3.0f;
        const float barX = boxMin.x - 5.0f;
        drawList->AddRectFilled(ImVec2(barX, boxMin.y), ImVec2(barX + barWidth, boxMax.y),
            IM_COL32(55, 55, 55, 255));
        const float healthHeight = (boxMax.y - boxMin.y) * (previewHealth / 100.0f);
        const int health = static_cast<int>(previewHealth);
        const ImU32 healthColor = IM_COL32((100 - health) * 255 / 100,
            health * 255 / 100, 0, 255);
        drawList->AddRectFilled(ImVec2(barX, boxMax.y - healthHeight), ImVec2(barX + barWidth, boxMax.y),
            healthColor);
    }

    const float previewFontSize = (std::clamp)(ImGui::GetFontSize() * 0.7f, 9.0f, 11.0f);
    ImFont* previewFont = ImGui::GetFont();
    if (showName)
    {
        const char* previewName = "ynestrosa";
        const ImVec2 textSize = previewFont->CalcTextSizeA(previewFontSize,
            FLT_MAX, 0.0f, previewName);
        const ImVec2 textPos(boxMin.x + (boxMax.x - boxMin.x - textSize.x) * 0.5f,
            boxMin.y - textSize.y - 3.0f);
        drawList->AddText(previewFont, previewFontSize,
            ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), IM_COL32(0, 0, 0, 230), previewName);
        drawList->AddText(previewFont, previewFontSize,
            textPos, IM_COL32(168, 155, 242, 255), previewName);
    }

    if (showFlags)
    {
        float flagY = imageMin.y + 4.0f;
        const float flagX = canvasMin.x + modelAreaWidth + flagGap;
        const auto drawFlag = [&](const char* text, ImU32 color)
        {
            drawList->AddText(previewFont, previewFontSize, ImVec2(flagX, flagY), color, text);
            flagY += previewFontSize + 3.0f;
        };
        drawFlag("SCOPED", IM_COL32(168, 155, 242, 255));
        drawFlag("FLASHED", IM_COL32(168, 155, 242, 255));

        const char* weaponLabel = "WEAPON";
        const ImVec2 weaponSize = previewFont->CalcTextSizeA(previewFontSize,
            FLT_MAX, 0.0f, weaponLabel);
        drawList->AddText(previewFont, previewFontSize,
            ImVec2(imageMin.x + (imageWidth - weaponSize.x) * 0.5f, imageMax.y + 3.0f),
            IM_COL32(168, 155, 242, 255), weaponLabel);

        if (!g_previewCounterTerrorist)
        {
            const char* c4Label = "C4";
            const ImVec2 c4Size = previewFont->CalcTextSizeA(previewFontSize,
                FLT_MAX, 0.0f, c4Label);
            const float c4Y = showName
                ? boxMin.y - previewFontSize - c4Size.y - 6.0f
                : boxMin.y - c4Size.y - 3.0f;
            drawList->AddText(previewFont, previewFontSize,
                ImVec2(boxMin.x + (boxMax.x - boxMin.x - c4Size.x) * 0.5f, c4Y),
                IM_COL32(255, 80, 95, 255), c4Label);
        }
    }

    drawList->PopClipRect();
    EndMenuPanel();
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
    LoadPreviewModel(pDevice);

    if (io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f) == nullptr)
    {
        io.Fonts->AddFontDefault();
    }

}

static void DrawCircleLabel(const char* label, float diameter)
{
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 center(cursor.x + diameter * 0.5f, cursor.y + diameter * 0.5f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCircleFilled(center, diameter * 0.5f, IM_COL32(12, 11, 18, 255), 32);
    drawList->AddCircle(center, diameter * 0.5f, IM_COL32(168, 155, 242, 210), 32, 1.3f);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
        IM_COL32(168, 155, 242, 255), label);
    ImGui::Dummy(ImVec2(diameter, diameter));
}

static bool DrawNamedTab(const char* label, int tab, int& activeTab, float width)
{
    const bool selected = activeTab == tab;
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.23f, 0.20f, 0.36f, 1.0f) : ImVec4(22.0f / 255.0f, 21.0f / 255.0f, 27.0f / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.13f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.17f, 0.32f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 7.0f));
    const bool pressed = ImGui::Button(label, ImVec2(width, 35.0f));
    if (selected)
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float underlineMin = min.x + 9.0f;
        const float underlineMax = max.x - 9.0f;
        const float underlineCenter = (underlineMin + underlineMax) * 0.5f;
        const float underlineTop = max.y - 3.0f;
        const float underlineBottom = max.y - 1.0f;
        const ImU32 transparentPurple = IM_COL32(168, 155, 242, 0);
        const ImU32 solidPurple = IM_COL32(168, 155, 242, 255);
        drawList->AddRectFilledMultiColor(
            ImVec2(underlineMin, underlineTop), ImVec2(underlineCenter, underlineBottom),
            transparentPurple, solidPurple, solidPurple, transparentPurple);
        drawList->AddRectFilledMultiColor(
            ImVec2(underlineCenter, underlineTop), ImVec2(underlineMax, underlineBottom),
            solidPurple, transparentPurple, transparentPurple, solidPurple);
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    if (pressed)
        activeTab = tab;
    return pressed;
}

void Menu::render()
{
    keybind.pollInputs();
    if (!showMenu)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 windowSize(740.0f, 620.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(700.0f, 600.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - windowSize.x) * 0.5f,
        (io.DisplaySize.y - windowSize.y) * 0.5f), ImGuiCond_FirstUseEver);

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
    if (!ImGui::Begin("Conglomerate", nullptr, windowFlags))
    {
        ImGui::End();
        return;
    }

    const float headerHeight = 43.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 3.0f));
    ImGui::BeginChild("TopBar", ImVec2(0.0f, headerHeight), false, ImGuiWindowFlags_NoScrollbar);
    DrawCircleLabel("CM", 34.0f);

    const float tabWidths[] = { 58.0f, 78.0f, 94.0f, 62.0f, 72.0f };
    const float tabsWidth = tabWidths[0] + tabWidths[1] + tabWidths[2] + tabWidths[3] + tabWidths[4] + 4.0f * 7.0f;
    const float profileWidth = 86.0f;
    const float tabsStart = (ImGui::GetWindowWidth() - tabsWidth) * 0.5f;
    ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 18.0f, tabsStart));

    const char* tabNames[] = { "Aim", "Visuals", "Movement", "Misc", "Config" };
    for (int i = 0; i < IM_ARRAYSIZE(tabNames); ++i)
    {
        DrawNamedTab(tabNames[i], i, activeTab, tabWidths[i]);
        if (i + 1 < IM_ARRAYSIZE(tabNames))
            ImGui::SameLine(0.0f, 7.0f);
    }

    const float profileStart = ImGui::GetWindowWidth() - profileWidth - ImGui::GetStyle().WindowPadding.x;
    ImGui::SameLine(profileStart);
    DrawCircleLabel("?", 30.0f);
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
        (30.0f - ImGui::GetTextLineHeight()) * 0.5f - 3.0f);
    ImGui::TextColored(kMenuPurple, "User");
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::Spacing();
    ImGui::BeginChild("ContentRegion", ImVec2(0.0f, 0.0f), false);
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float gap = ImGui::GetStyle().ItemSpacing.x;

    switch (activeTab)
    {
    case 0:
    {
        const float optionsWidth = contentSize.x;
        ImGui::BeginChild("AimColumns", ImVec2(optionsWidth, 0.0f), false, ImGuiWindowFlags_NoScrollbar);
        const float cardGap = ImGui::GetStyle().ItemSpacing.x;
        const float cardWidth = (optionsWidth - cardGap) * 0.5f;

        BeginMenuPanel("AimPanel", "Aim", ImVec2(cardWidth, 0.0f));
        MenuCheckbox("Enable##AimBot", &Config::aimbot);
        ImGui::TextUnformatted("Activation key");
        keybind.menuButton(Config::aimbot);
        MenuCheckbox("Team Check", &Config::team_check);
        MenuSliderFloat("FOV", &Config::aimbot_fov, 0.0f, 90.0f);
        MenuCheckbox("Draw FOV Circle", &Config::fov_circle);
        if (Config::fov_circle)
            ColorEditHexOnly("Circle Color", Config::fovCircleColor);
        EndMenuPanel();

        ImGui::SameLine(0.0f, cardGap);
        BeginMenuPanel("TriggerPanel", "Triggerbot", ImVec2(0.0f, 0.0f));
        MenuCheckbox("Enable##Trigger", &Config::triggerbot);
        ImGui::TextUnformatted("Activation key");
        keybind.menuButton(Config::triggerbot);
        MenuCheckbox("Team Check##Triggerbot", &Config::triggerbot_team_check);
        EndMenuPanel();
        ImGui::EndChild();
        break;
    }
    case 1:
    {
        const float previewWidth = contentSize.x * 0.43f;
        const float optionsWidth = contentSize.x - previewWidth - gap;
        ImGui::BeginChild("VisualOptions", ImVec2(optionsWidth, 0.0f), false, ImGuiWindowFlags_NoScrollbar);
        const float visualsHeight = ImGui::GetContentRegionAvail().y;
        const float espHeight = visualsHeight * 0.58f;
        BeginMenuPanel("ESPPanel", "ESP", ImVec2(0.0f, espHeight));
        MenuCheckbox("Enable##ESP", &Config::esp);
        MenuSliderFloat("Thickness", &Config::espThickness, 1.0f, 5.0f);
        MenuCheckbox("Box Fill", &Config::espFill);
        if (Config::espFill)
            MenuSliderFloat("Fill Opacity", &Config::espFillOpacity, 0.0f, 1.0f);
        ColorEditHexOnly("T Color", Config::espColorT);
        ColorEditHexOnly("CT Color", Config::espColorCT);
        MenuCheckbox("Team Check", &Config::teamCheck);
        MenuCheckbox("Health Bar", &Config::showHealth);
        MenuCheckbox("Name Tags", &Config::showNameTags);
        MenuCheckbox("Flags", &Config::flags);
        MenuCheckbox("Skeleton ESP", &Config::skeleton);
        if (Config::skeleton)
        {
            MenuSliderFloat("Skeleton Thickness", &Config::skeletonThickness, 1.0f, 5.0f);
            ColorEditHexOnly("Skeleton Color", Config::skeletonColor);
        }
        ImGui::Spacing();
        EndMenuPanel();

        ImGui::Spacing();
        BeginMenuPanel("ChamsPanel", "Chams", ImVec2(0.0f, 0.0f));
        const char* chamsMaterials[] = { "Flat", "Illuminate", "Glow" };
        MenuCheckbox("Visible Chams", &Config::enemyChams);
        ImGui::Combo("Material##VisibleChamsMaterial", &Config::chamsMaterial, chamsMaterials, IM_ARRAYSIZE(chamsMaterials));
        if (Config::enemyChams)
            ColorEditHexOnly("Visible Chams Color", Config::colVisualChams);
        MenuCheckbox("Occluded Chams", &Config::enemyChamsInvisible);
        if (Config::enemyChamsInvisible)
            ColorEditHexOnly("Occluded Chams Color", Config::colVisualChamsIgnoreZ);
        MenuCheckbox("Viewmodel Chams", &Config::armChams);
        if (Config::armChams)
        {
            ImGui::Combo("Material##ViewmodelChamsMaterial", &Config::armChamsMaterial, chamsMaterials, IM_ARRAYSIZE(chamsMaterials));
            ColorEditHexOnly("Viewmodel Color", Config::colArmChams);
        }
        MenuCheckbox("Gun Chams", &Config::viewmodelChams);
        if (Config::viewmodelChams)
        {
            ImGui::Combo("Material##GunChamsMaterial", &Config::viewmodelChamsMaterial, chamsMaterials, IM_ARRAYSIZE(chamsMaterials));
            ColorEditHexOnly("Gun Color", Config::colViewmodelChams);
        }
        ImGui::Spacing();
        ImGui::TextColored(kMenuPurple, "Removals");
        ImGui::Separator();
        MenuCheckbox("Anti Flash", &Config::antiflash);
        MenuCheckbox("Remove Smoke", &Config::noSmoke);
        EndMenuPanel();
        ImGui::EndChild();

        ImGui::SameLine(0.0f, gap);
        RenderPreviewPanel(ImVec2(0.0f, 0.0f));
        break;
    }
    case 2:
    {
        BeginMenuPanel("MovementPanel", "Movement", ImVec2(0.0f, 0.0f));
        MenuCheckbox("Bunny Hop", &Config::bunnyHop);
        EndMenuPanel();
        break;
    }
    case 3:
    {
        BeginMenuPanel("MiscPanel", "Misc", ImVec2(0.0f, 0.0f));
        MenuCheckbox("Watermark", &Config::watermark);
        MenuCheckbox("Spectator List", &Config::spectatorList);
        ImGui::Spacing();
        MenuCheckbox("Night Mode", &Config::Night);
        if (Config::Night)
            ColorEditHexOnly("Night Color", Config::NightColor);
        MenuCheckbox("Custom FOV", &Config::fovEnabled);
        if (Config::fovEnabled)
            MenuSliderFloat("FOV Value##FovSlider", &Config::fov, 20.0f, 160.0f, "%1.0f");
        EndMenuPanel();
        break;
    }
    case 4:
    {
        BeginMenuPanel("ConfigPanel", "Config", ImVec2(0.0f, 0.0f));
        static char configName[128] = "";
        static std::vector<std::string> configList = internal_config::ConfigManager::ListConfigs();
        static int selectedConfigIndex = -1;

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(14.0f / 255.0f, 13.0f / 255.0f, 17.0f / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(28.0f / 255.0f, 26.0f / 255.0f, 34.0f / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(38.0f / 255.0f, 35.0f / 255.0f, 45.0f / 255.0f, 1.0f));
        ImGui::InputText("Config Name", configName, IM_ARRAYSIZE(configName));
        ImGui::PopStyleColor(3);
        if (ImGui::Button("Refresh"))
            configList = internal_config::ConfigManager::ListConfigs();
        ImGui::SameLine();
        if (ImGui::Button("Load"))
            internal_config::ConfigManager::Load(configName);
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            internal_config::ConfigManager::Save(configName);
            configList = internal_config::ConfigManager::ListConfigs();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            internal_config::ConfigManager::Remove(configName);
            configList = internal_config::ConfigManager::ListConfigs();
        }

        ImGui::Spacing();
        ImGui::TextColored(kMenuPurple, "Saved Configs");
        ImGui::Separator();
        for (int i = 0; i < static_cast<int>(configList.size()); ++i)
        {
            if (ImGui::Selectable(configList[i].c_str(), selectedConfigIndex == i))
            {
                selectedConfigIndex = i;
                strncpy_s(configName, sizeof(configName), configList[i].c_str(), _TRUNCATE);
            }
        }
        EndMenuPanel();
        break;
    }
    }

    ImGui::EndChild();
    ImGui::End();
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
