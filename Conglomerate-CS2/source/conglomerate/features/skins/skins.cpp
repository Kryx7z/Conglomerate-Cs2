#include "skins.h"
#include "skins_catalog.h"

#include "../../../cs2/entity/C_CSPlayerPawn/C_CSPlayerPawn.h"
#include "../../../conglomerate/offsets/offsets.h"
#include "../../../conglomerate/interfaces/interfaces.h"
#include "../../../conglomerate/utils/memory/patternscan/patternscan.h"
#include "../../../conglomerate/utils/memory/vfunc/vfunc.h"
#include "../../../conglomerate/utils/debug_console.h"
#include "../../../../external/imgui/imgui.h"
#include "../../../../external/json/json.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <winhttp.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

extern ID3D11Device* pDevice;

namespace {
	struct DynamicSchemaOffset {
		const char* path;
		operator std::ptrdiff_t() const
		{
			const std::uint32_t resolved = SchemaFinder::Get(path);
		return static_cast<std::ptrdiff_t>(resolved);
		}
	};
}

namespace Offset {
	namespace C_BaseEntity {
		inline const DynamicSchemaOffset m_pGameSceneNode{"C_BaseEntity->m_pGameSceneNode"};
		inline const DynamicSchemaOffset m_nSubclassID{"C_BaseEntity->m_nSubclassID"};
	}
	namespace C_BasePlayerPawn {
		inline const DynamicSchemaOffset m_hController{"C_BasePlayerPawn->m_hController"};
		inline const DynamicSchemaOffset m_EconGloves{"C_CSPlayerPawn->m_EconGloves"};
		inline const DynamicSchemaOffset m_bNeedToReApplyGloves{"C_CSPlayerPawn->m_bNeedToReApplyGloves"};
	}
	namespace CBasePlayerController {
		inline const DynamicSchemaOffset m_steamID{"CBasePlayerController->m_steamID"};
	}
	namespace C_EconEntity {
		inline const DynamicSchemaOffset m_nFallbackPaintKit{"C_EconEntity->m_nFallbackPaintKit"};
		inline const DynamicSchemaOffset m_nFallbackSeed{"C_EconEntity->m_nFallbackSeed"};
		inline const DynamicSchemaOffset m_flFallbackWear{"C_EconEntity->m_flFallbackWear"};
		inline const DynamicSchemaOffset m_nFallbackStatTrak{"C_EconEntity->m_nFallbackStatTrak"};
		inline const DynamicSchemaOffset m_AttributeManager{"C_EconEntity->m_AttributeManager"};
	}
	namespace C_AttributeContainer {
		inline const DynamicSchemaOffset m_Item{"C_AttributeContainer->m_Item"};
	}
	namespace C_EconItemView {
		inline const DynamicSchemaOffset m_iItemID{"C_EconItemView->m_iItemID"};
		inline const DynamicSchemaOffset m_iItemIDHigh{"C_EconItemView->m_iItemIDHigh"};
		inline const DynamicSchemaOffset m_iItemIDLow{"C_EconItemView->m_iItemIDLow"};
		inline const DynamicSchemaOffset m_iAccountID{"C_EconItemView->m_iAccountID"};
		inline const DynamicSchemaOffset m_bInitialized{"C_EconItemView->m_bInitialized"};
		inline const DynamicSchemaOffset m_bDisallowSOC{"C_EconItemView->m_bDisallowSOC"};
		inline const DynamicSchemaOffset m_bRestoreCustomMaterialAfterPrecache{"C_EconItemView->m_bRestoreCustomMaterialAfterPrecache"};
		inline const DynamicSchemaOffset m_iItemDefinitionIndex{"C_EconItemView->m_iItemDefinitionIndex"};
		inline const DynamicSchemaOffset m_iEntityQuality{"C_EconItemView->m_iEntityQuality"};
	}
}

namespace Skins {
namespace {
	using SkinsCatalog::AgentEntry;
	using SkinsCatalog::SkinEntry;

	constexpr uint32_t kKnifeFauxItemHandle = 0x4B4E4946u; // Stable identifier for the custom knife item.

	constexpr uint64_t makeFauxItemId(uint32_t itemHandle)
	{
		return 0xF000000000000000ull | static_cast<uint64_t>(itemHandle);
	}

	struct KnifeEntry {
		int definition;
		const char* name;
	};

	struct SelectedSkin {
		SkinEntry skin;
		float wear = 0.08f;
		int seed = 0;
		int statTrak = -1;
	};

	struct WeaponBaseline {
		int paintKit;
		int seed;
		float wear;
		int statTrak;
		uint64_t itemId;
		uint32_t itemIdHigh;
		uint32_t itemIdLow;
		uint32_t accountId;
		uint8_t initialized;
		uint8_t disallowSoc;
		uint8_t restoreCustomMaterial;
		uint16_t definition;
		int quality;
		uint32_t subclass;
	};

	struct AppliedWeaponSkin {
		int definition = -1;
		int paintKit = -1;
		int seed = -1;
		int statTrak = -2;
		float wear = -1.0f;
		int knifeIndex = -1;
		uint64_t refreshRevision = 0;
	};

	constexpr std::array<KnifeEntry, 21> kKnives = {{
		{500, "Bayonet"}, {503, "Classic"}, {505, "Flip"}, {506, "Gut"},
		{507, "Karambit"}, {508, "M9 Bayonet"}, {509, "Huntsman"}, {512, "Falchion"},
		{514, "Bowie"}, {515, "Butterfly"}, {516, "Shadow Daggers"}, {517, "Paracord"},
		{518, "Survival"}, {519, "Ursus"}, {520, "Navaja"}, {521, "Nomad"},
		{522, "Stiletto"}, {523, "Talon"}, {525, "Skeleton"}, {526, "Kukri"},
		{0, nullptr}
	}};

	constexpr std::array<std::pair<int, const char*>, 8> kGloveTypes = {{
		{5027, "Bloodhound Gloves"}, {5030, "Sport Gloves"},
		{5031, "Driver Gloves"}, {5032, "Hand Wraps"},
		{5033, "Moto Gloves"}, {5034, "Specialist Gloves"},
		{5035, "Hydra Gloves"}, {4725, "Broken Fang Gloves"}
	}};

	std::vector<SkinEntry> g_skins;
	std::vector<AgentEntry> g_agents;
	std::unordered_map<int, std::vector<const SkinEntry*>> g_weaponSkinsByDefinition;
	std::mutex g_stateMutex;
	std::atomic_bool g_loadStarted{false};
	std::atomic_bool g_catalogReady{false};
	std::atomic_bool g_catalogFailed{false};

	enum class Category { Weapons, Knives, Gloves, Agents };
	Category g_category = Category::Weapons;
	int g_weaponId = 7;
	int g_knifeIndex = 0;
	int g_gloveIndex = 0;
	std::array<int, 4> g_skinIndexByCategory{{ -1, -1, -1, -1 }};
	int g_agentIndex = -1;
	std::array<float, 4> g_wearByCategory{{ 0.08f, 0.08f, 0.08f, 0.08f }};
	std::array<int, 4> g_seedByCategory{};
	std::array<int, 4> g_statTrakByCategory{{ -1, -1, -1, -1 }};
	std::array<double, 4> g_skinAppliedFeedbackUntil{};
	std::array<std::array<char, 96>, 4> g_searchByCategory{};
	std::unordered_map<int, SelectedSkin> g_weaponSelection;
	std::unordered_map<uintptr_t, WeaponBaseline> g_weaponBaselines;
	std::unordered_map<uintptr_t, CBaseHandle> g_weaponHandles;
	std::unordered_map<uintptr_t, AppliedWeaponSkin> g_appliedWeaponSkins;
	std::unordered_map<uintptr_t, ULONGLONG> g_failedSkinApplyRetryAt;
	uint64_t g_skinRefreshRevision = 1;
	SelectedSkin g_knifeSkin{};
	bool g_hasKnifeSkin = false;
	int g_selectedKnife = -1;
	SelectedSkin g_gloveSkin{};
	bool g_hasGloveSkin = false;
	int g_selectedGloveDefinition = 0;
	uintptr_t g_lastGlovePawn = 0;
	int g_lastGlovePaint = -1;
	int g_lastGloveSeed = -1;
	float g_lastGloveWear = -1.0f;
	std::string g_agentCtModel;
	std::string g_agentTModel;
	std::string g_error;
	nlohmann::json g_pendingSkinConfig;
	bool g_hasPendingSkinConfig = false;

	int& activeSkinIndex()
	{
		return g_skinIndexByCategory[static_cast<size_t>(g_category)];
	}

	char* activeSearch()
	{
		return g_searchByCategory[static_cast<size_t>(g_category)].data();
	}

	float& activeWear()
	{
		return g_wearByCategory[static_cast<size_t>(g_category)];
	}

	int& activeSeed()
	{
		return g_seedByCategory[static_cast<size_t>(g_category)];
	}

	int& activeStatTrak()
	{
		return g_statTrakByCategory[static_cast<size_t>(g_category)];
	}

	enum class PreviewLoadState : uint8_t { NotRequested, Loading, Decoded, Ready, Failed };
	struct PreviewImageEntry {
		std::atomic<PreviewLoadState> state{ PreviewLoadState::NotRequested };
		std::vector<uint8_t> rgba;
		UINT width = 0;
		UINT height = 0;
		ID3D11ShaderResourceView* texture = nullptr;
		uint64_t lastUseFrame = 0;
		~PreviewImageEntry() { if (texture) texture->Release(); }
	};
	std::unordered_map<std::string, std::shared_ptr<PreviewImageEntry>> g_previewImages;
	std::mutex g_previewImagesMutex;
	std::atomic_uint g_previewWorkers{ 0 };
	std::atomic_uint64_t g_previewFrame{ 0 };

	struct PreviewHttpHandle {
		HINTERNET value = nullptr;
		~PreviewHttpHandle() { if (value) WinHttpCloseHandle(value); }
	};

	template <typename T>
	struct PreviewComRelease {
		void operator()(T* value) const { if (value) value->Release(); }
	};

	template <typename T>
	using PreviewComPtr = std::unique_ptr<T, PreviewComRelease<T>>;

	bool downloadPreview(const std::string& url, std::vector<uint8_t>& bytes)
	{
		if (url.rfind("https://", 0) != 0) return false;
		const std::wstring wideUrl(url.begin(), url.end());
		URL_COMPONENTS parts{};
		parts.dwStructSize = sizeof(parts);
		parts.dwHostNameLength = static_cast<DWORD>(-1);
		parts.dwUrlPathLength = static_cast<DWORD>(-1);
		parts.dwExtraInfoLength = static_cast<DWORD>(-1);
		if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
			return false;
		const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
		if (_wcsicmp(host.c_str(), L"community.akamai.steamstatic.com") != 0)
			return false;

		std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
		if (parts.dwExtraInfoLength)
			path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
		PreviewHttpHandle session{ WinHttpOpen(L"Conglomerate UI", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
		if (!session.value) return false;
		WinHttpSetTimeouts(session.value, 3000, 3000, 5000, 5000);
		PreviewHttpHandle connection{ WinHttpConnect(session.value, host.c_str(), parts.nPort, 0) };
		if (!connection.value) return false;
		PreviewHttpHandle request{ WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) };
		if (!request.value || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
			WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr))
			return false;

		DWORD status = 0;
		DWORD statusSize = sizeof(status);
		if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) || status != 200)
			return false;

		constexpr size_t maxImageBytes = 8 * 1024 * 1024;
		bytes.clear();
		for (;;) {
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request.value, &available)) return false;
			if (!available) break;
			if (bytes.size() + available > maxImageBytes) return false;
			const size_t oldSize = bytes.size();
			bytes.resize(oldSize + available);
			DWORD received = 0;
			if (!WinHttpReadData(request.value, bytes.data() + oldSize, available, &received)) return false;
			bytes.resize(oldSize + received);
			if (!received) break;
		}
		return !bytes.empty();
	}

	bool decodePreview(const std::vector<uint8_t>& encoded, PreviewImageEntry& entry)
	{
		if (encoded.empty() || encoded.size() > MAXDWORD) return false;
		const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool uninitializeCom = SUCCEEDED(comResult);
		if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return false;

		bool decoded = false;
		IWICImagingFactory* rawFactory = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&rawFactory)))) {
			PreviewComPtr<IWICImagingFactory> factory(rawFactory);
			IWICStream* rawStream = nullptr;
			if (SUCCEEDED(factory->CreateStream(&rawStream))) {
				PreviewComPtr<IWICStream> stream(rawStream);
				if (SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()),
					static_cast<DWORD>(encoded.size())))) {
					IWICBitmapDecoder* rawDecoder = nullptr;
					if (SUCCEEDED(factory->CreateDecoderFromStream(stream.get(), nullptr,
						WICDecodeMetadataCacheOnLoad, &rawDecoder))) {
						PreviewComPtr<IWICBitmapDecoder> decoder(rawDecoder);
						IWICBitmapFrameDecode* rawFrame = nullptr;
						if (SUCCEEDED(decoder->GetFrame(0, &rawFrame))) {
							PreviewComPtr<IWICBitmapFrameDecode> frame(rawFrame);
							IWICFormatConverter* rawConverter = nullptr;
							if (SUCCEEDED(factory->CreateFormatConverter(&rawConverter))) {
								PreviewComPtr<IWICFormatConverter> converter(rawConverter);
								if (SUCCEEDED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA,
									WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
									UINT width = 0, height = 0;
									if (SUCCEEDED(converter->GetSize(&width, &height)) && width && height &&
										width <= 2048 && height <= 2048 &&
										height <= (MAXDWORD / (width * 4u))) {
										const UINT stride = width * 4u;
										std::vector<uint8_t> rgba(static_cast<size_t>(stride) * height);
										if (SUCCEEDED(converter->CopyPixels(nullptr, stride,
											static_cast<UINT>(rgba.size()), rgba.data()))) {
											entry.rgba = std::move(rgba);
											entry.width = width;
											entry.height = height;
											decoded = true;
										}
									}
								}
							}
						}
					}
				}
			}
		}
		if (uninitializeCom) CoUninitialize();
		return decoded;
	}

	ID3D11ShaderResourceView* previewTexture(const std::string& url, UINT& width, UINT& height)
	{
		if (url.empty() || !pDevice) return nullptr;
		std::shared_ptr<PreviewImageEntry> entry;
		{
			std::lock_guard<std::mutex> lock(g_previewImagesMutex);
			auto& slot = g_previewImages[url];
			if (!slot) slot = std::make_shared<PreviewImageEntry>();
			entry = slot;
			entry->lastUseFrame = g_previewFrame.load(std::memory_order_relaxed);
		}

		PreviewLoadState state = entry->state.load(std::memory_order_acquire);
		if (state == PreviewLoadState::NotRequested) {
			if (g_previewWorkers.fetch_add(1, std::memory_order_acq_rel) < 4) {
				PreviewLoadState expected = PreviewLoadState::NotRequested;
				if (entry->state.compare_exchange_strong(expected, PreviewLoadState::Loading,
					std::memory_order_acq_rel)) {
					try {
						std::thread([entry, url] {
							try {
								std::vector<uint8_t> encoded;
								entry->state.store(downloadPreview(url, encoded) && decodePreview(encoded, *entry)
									? PreviewLoadState::Decoded : PreviewLoadState::Failed, std::memory_order_release);
							} catch (...) {
								entry->state.store(PreviewLoadState::Failed, std::memory_order_release);
							}
							g_previewWorkers.fetch_sub(1, std::memory_order_acq_rel);
						}).detach();
					} catch (...) {
						entry->state.store(PreviewLoadState::Failed, std::memory_order_release);
						g_previewWorkers.fetch_sub(1, std::memory_order_acq_rel);
					}
				} else {
					g_previewWorkers.fetch_sub(1, std::memory_order_acq_rel);
				}
			} else {
				g_previewWorkers.fetch_sub(1, std::memory_order_acq_rel);
			}
			return nullptr;
		}

		state = entry->state.load(std::memory_order_acquire);
		if (state == PreviewLoadState::Decoded) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = entry->width;
			desc.Height = entry->height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA data{};
			data.pSysMem = entry->rgba.data();
			data.SysMemPitch = entry->width * 4u;
			ID3D11Texture2D* texture = nullptr;
			if (SUCCEEDED(pDevice->CreateTexture2D(&desc, &data, &texture))) {
				const HRESULT result = pDevice->CreateShaderResourceView(texture, nullptr, &entry->texture);
				texture->Release();
				if (SUCCEEDED(result)) {
					entry->rgba.clear();
					entry->rgba.shrink_to_fit();
					entry->state.store(PreviewLoadState::Ready, std::memory_order_release);
				} else {
					entry->state.store(PreviewLoadState::Failed, std::memory_order_release);
				}
			} else {
				entry->state.store(PreviewLoadState::Failed, std::memory_order_release);
			}
		}

		if (entry->state.load(std::memory_order_acquire) != PreviewLoadState::Ready) return nullptr;
		width = entry->width;
		height = entry->height;
		return entry->texture;
	}

	void beginPreviewFrame()
	{
		const uint64_t frame = g_previewFrame.fetch_add(1, std::memory_order_relaxed) + 1;
		std::lock_guard<std::mutex> lock(g_previewImagesMutex);
		constexpr size_t maxCachedPreviews = 256;
		if (g_previewImages.size() <= maxCachedPreviews) return;

		auto oldest = g_previewImages.end();
		for (auto it = g_previewImages.begin(); it != g_previewImages.end(); ++it) {
			if (it->second->state.load(std::memory_order_acquire) == PreviewLoadState::Loading ||
				frame <= it->second->lastUseFrame + 2)
				continue;
			if (oldest == g_previewImages.end() || it->second->lastUseFrame < oldest->second->lastUseFrame)
				oldest = it;
		}
		if (oldest != g_previewImages.end()) g_previewImages.erase(oldest);
	}

	PreviewLoadState drawPreviewImage(ImDrawList* drawList, const std::string& url, const ImVec2& min, const ImVec2& max)
	{
		if (!drawList || !ImGui::IsRectVisible(min, max)) return PreviewLoadState::NotRequested;
		if (url.empty() || !pDevice) return PreviewLoadState::Failed;
		UINT width = 0, height = 0;
		ID3D11ShaderResourceView* texture = previewTexture(url, width, height);
		if (!texture || !width || !height) {
			std::lock_guard<std::mutex> lock(g_previewImagesMutex);
			const auto entry = g_previewImages.find(url);
			return entry == g_previewImages.end()
				? PreviewLoadState::NotRequested
				: entry->second->state.load(std::memory_order_acquire);
		}
		const float boxWidth = max.x - min.x;
		const float boxHeight = max.y - min.y;
		const float scale = (std::min)(boxWidth / static_cast<float>(width), boxHeight / static_cast<float>(height));
		const ImVec2 size(static_cast<float>(width) * scale, static_cast<float>(height) * scale);
		const ImVec2 imageMin(min.x + (boxWidth - size.x) * 0.5f, min.y + (boxHeight - size.y) * 0.5f);
		drawList->AddImage(reinterpret_cast<ImTextureID>(texture), imageMin,
			ImVec2(imageMin.x + size.x, imageMin.y + size.y));
		return PreviewLoadState::Ready;
	}

	void drawPreviewStatus(ImDrawList* drawList, const std::string& url, PreviewLoadState state,
		const ImVec2& min, const ImVec2& max)
	{
		if (!drawList || state == PreviewLoadState::Ready || !ImGui::IsRectVisible(min, max)) return;
		const char* text = url.empty() ? "No preview available"
			: state == PreviewLoadState::Failed ? "Preview unavailable" : "Loading preview...";
		const ImVec2 textSize = ImGui::CalcTextSize(text);
		drawList->AddText(ImVec2(min.x + ((max.x - min.x) - textSize.x) * 0.5f,
			min.y + ((max.y - min.y) - textSize.y) * 0.5f), IM_COL32(155, 151, 166, 255), text);
	}

	using SetItemAttributeFn = void(__fastcall*)(void*, const char*, float);
	using AddStatTrakEntityFn = void(__fastcall*)(void*, void*);
	using ChangeModelFn = void(__fastcall*)(void*, const char*);
	using GetViewModelFn = void(__fastcall*)(void*);
	using GetModelPathFn = const char*(__fastcall*)(void*);
	using ApplyEconCustomizationFn = void(__fastcall*)(void*, bool);
	using UpdateSubclassFn = void(__fastcall*)(void*);
	using RegenerateWeaponSkinsFn = void(__fastcall*)();
	using RegenerateWeaponSkinFn = void(__fastcall*)(void*, bool);
	using SetMeshGroupMaskFn = void(__fastcall*)(void*, uint64_t);
	using InvalidateItemDescriptionFn = void(__fastcall*)(void*);
	using SetBodygroupFn = void(__fastcall*)(void*, int, uint32_t);
	SetItemAttributeFn g_setItemAttribute = nullptr;
	AddStatTrakEntityFn g_addStatTrakEntity = nullptr;
	ChangeModelFn g_changeModel = nullptr;
	GetViewModelFn g_getViewModel = nullptr;
	GetModelPathFn g_getModelPath = nullptr;
	ApplyEconCustomizationFn g_applyEconCustomization = nullptr;
	UpdateSubclassFn g_updateSubclass = nullptr;
	RegenerateWeaponSkinsFn g_regenerateWeaponSkins = nullptr;
	RegenerateWeaponSkinFn g_regenerateWeaponSkin = nullptr;
	SetMeshGroupMaskFn g_setMeshGroupMask = nullptr;
	InvalidateItemDescriptionFn g_invalidateItemDescription = nullptr;
	SetBodygroupFn g_setBodygroup = nullptr;
	std::once_flag g_initializeFunctionsOnce;
	uintptr_t g_lastAgentPawn = 0;
	std::string g_lastAgentModel;
	uintptr_t g_lastActiveWeapon = 0;
	uintptr_t g_lastHudModelWeapon = 0;
	uintptr_t g_lastHudModelEntity = 0;
	std::atomic<uint64_t> g_activeHudMeshGroup{ 1 };
	std::atomic<uint64_t> g_lastRenderedHudMaskState{ 0 };
	std::atomic_bool g_skinAlive{ false };
	bool g_skinDeathHandled = false;
	bool g_postRespawnSkinReapplyPending = false;
	bool g_weaponSkinRegenerationPending = false;
	constexpr ULONGLONG kFailedSkinApplyRetryDelayMs = 350;

	bool skinApplyRetryDeferred(uintptr_t weapon)
	{
		const auto retry = g_failedSkinApplyRetryAt.find(weapon);
		return retry != g_failedSkinApplyRetryAt.end() && GetTickCount64() < retry->second;
	}

	void deferFailedSkinApply(uintptr_t weapon)
	{
		g_failedSkinApplyRetryAt[weapon] = GetTickCount64() + kFailedSkinApplyRetryDelayMs;
	}

	uintptr_t resolveRegenerateWeaponSkin(uintptr_t globalRegenerator)
	{
		if (!globalRegenerator) return 0;
		constexpr uint8_t callPrefix[] = { 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8 };
		uintptr_t resolvedTarget = 0;
		bool ambiguousTarget = false;
		__try {
			for (size_t i = 0; i < 0x100 - sizeof(callPrefix); ++i) {
				bool matches = true;
				for (size_t j = 0; j < sizeof(callPrefix); ++j) {
					if (*reinterpret_cast<const uint8_t*>(globalRegenerator + i + j) != callPrefix[j]) {
						matches = false;
						break;
					}
				}
				if (!matches) continue;
				const int32_t relativeTarget = *reinterpret_cast<const int32_t*>(globalRegenerator + i + sizeof(callPrefix));
				const uintptr_t candidate = globalRegenerator + i + sizeof(callPrefix) + sizeof(relativeTarget) + relativeTarget;
				if (resolvedTarget) {
					ambiguousTarget = true;
					break;
				}
				resolvedTarget = candidate;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return 0;
		}
		return ambiguousTarget ? 0 : resolvedTarget;
	}

	void requestSkinRefresh()
	{
		++g_skinRefreshRevision;
		if (g_skinRefreshRevision == 0) g_skinRefreshRevision = 1;
		DebugConsole::logf("[skins] refresh requested revision=%llu",
			static_cast<unsigned long long>(g_skinRefreshRevision));
	}

	bool requiredSkinSchemaAvailable()
	{
		static const char* requiredFields[] = {
			"C_BaseEntity->m_pGameSceneNode", "C_BaseEntity->m_nSubclassID",
			"C_BasePlayerPawn->m_hController", "CBasePlayerController->m_steamID",
			"C_CSPlayerPawn->m_hHudModelArms", "CGameSceneNode->m_pChild",
			"CGameSceneNode->m_pOwner", "CGameSceneNode->m_pNextSibling",
			"C_EconEntity->m_nFallbackPaintKit", "C_EconEntity->m_nFallbackSeed",
			"C_EconEntity->m_flFallbackWear", "C_EconEntity->m_nFallbackStatTrak",
			"C_EconEntity->m_AttributeManager", "C_AttributeContainer->m_Item",
			"C_EconItemView->m_iItemID", "C_EconItemView->m_iItemIDHigh",
			"C_EconItemView->m_iItemIDLow", "C_EconItemView->m_iAccountID",
			"C_EconItemView->m_bInitialized", "C_EconItemView->m_bDisallowSOC",
			"C_EconItemView->m_bRestoreCustomMaterialAfterPrecache",
			"C_EconItemView->m_iItemDefinitionIndex", "C_EconItemView->m_iEntityQuality"
		};
		for (const char* field : requiredFields) {
			if (!SchemaFinder::Get(field)) {
				DebugConsole::once("skins.schema_missing", "[skins] required schema field unavailable: %s", field);
				return false;
			}
		}
		return true;
	}

	bool requiredGloveSchemaAvailable()
	{
		static const char* requiredFields[] = {
			"C_CSPlayerPawn->m_EconGloves", "C_CSPlayerPawn->m_bNeedToReApplyGloves",
			"C_EconItemView->m_iItemDefinitionIndex", "C_EconItemView->m_iItemID",
			"C_EconItemView->m_iItemIDHigh", "C_EconItemView->m_iItemIDLow",
			"C_EconItemView->m_iAccountID", "C_EconItemView->m_bInitialized",
			"C_EconItemView->m_bRestoreCustomMaterialAfterPrecache"
		};
		for (const char* field : requiredFields) {
			if (!SchemaFinder::Get(field)) {
				DebugConsole::once("skins.glove_schema_missing", "[skins] required glove schema field unavailable: %s", field);
				return false;
			}
		}
		return true;
	}

	const char* weaponName(int id)
	{
		switch (id) {
		case 1: return "Desert Eagle"; case 2: return "Dual Berettas";
		case 3: return "Five-SeveN"; case 4: return "Glock-18";
		case 7: return "AK-47"; case 8: return "AUG"; case 9: return "AWP";
		case 10: return "FAMAS"; case 11: return "G3SG1"; case 13: return "Galil AR";
		case 14: return "M249"; case 16: return "M4A4"; case 17: return "MAC-10";
		case 19: return "P90"; case 23: return "MP5-SD"; case 24: return "UMP-45";
		case 25: return "XM1014"; case 26: return "PP-Bizon"; case 27: return "MAG-7";
		case 28: return "Negev"; case 29: return "Sawed-Off"; case 30: return "Tec-9";
		case 31: return "Zeus x27"; case 32: return "P2000"; case 33: return "MP7";
		case 34: return "MP9"; case 35: return "Nova"; case 36: return "P250";
		case 38: return "SCAR-20"; case 39: return "SG 553"; case 40: return "SSG 08";
		case 60: return "M4A1-S"; case 61: return "USP-S"; case 63: return "CZ75-Auto";
		case 64: return "R8 Revolver"; default: return "Weapon";
		}
	}

	std::string readString(const nlohmann::json& value, const char* key)
	{
		if (!value.is_object() || !value.contains(key)) return {};
		const auto& field = value[key];
		if (field.is_string()) return field.get<std::string>();
		if (field.is_object() && field.contains("en") && field["en"].is_string())
			return field["en"].get<std::string>();
		return {};
	}

	int readInt(const nlohmann::json& value, const char* key, int fallback = 0)
	{
		if (!value.is_object() || !value.contains(key)) return fallback;
		const auto& field = value[key];
		if (field.is_number_integer()) return field.get<int>();
		if (field.is_string()) {
			try { return std::stoi(field.get<std::string>()); } catch (...) {}
		}
		return fallback;
	}

	float readFloat(const nlohmann::json& value, const char* key, float fallback)
	{
		if (!value.is_object() || !value.contains(key)) return fallback;
		const auto& field = value[key];
		if (field.is_number()) return field.get<float>();
		return fallback;
	}

	bool readBool(const nlohmann::json& value, const char* key, bool fallback)
	{
		if (!value.is_object() || !value.contains(key)) return fallback;
		const auto& field = value[key];
		return field.is_boolean() ? field.get<bool>() : fallback;
	}

	const SkinEntry* findSkinEntry(int definition, int paintKit, bool knife, bool glove)
	{
		for (const auto& skin : g_skins) {
			if (skin.weaponId == definition && skin.paintKit == paintKit &&
				skin.knife == knife && skin.glove == glove)
				return &skin;
		}
		return nullptr;
	}

	float configWear(const nlohmann::json& value, const SkinEntry& skin)
	{
		return std::clamp(readFloat(value, "wear", 0.08f), skin.minWear,
			(std::max)(skin.minWear + 0.001f, skin.maxWear));
	}

	void applyPendingSkinConfig()
	{
		if (!g_hasPendingSkinConfig || !g_catalogReady.load(std::memory_order_acquire)) return;
		const nlohmann::json config = std::move(g_pendingSkinConfig);
		g_pendingSkinConfig = nlohmann::json{};
		g_hasPendingSkinConfig = false;

		g_weaponSelection.clear();
		g_hasKnifeSkin = false;
		g_selectedKnife = -1;
		g_hasGloveSkin = false;
		g_selectedGloveDefinition = 0;
		g_agentCtModel.clear();
		g_agentTModel.clear();
		g_agentIndex = -1;
		g_skinIndexByCategory.fill(-1);
		g_wearByCategory.fill(0.08f);
		g_seedByCategory.fill(0);
		g_statTrakByCategory.fill(-1);
		g_skinAppliedFeedbackUntil.fill(0.0);
		for (auto& search : g_searchByCategory) search.fill('\0');

		if (config.contains("weapons") && config["weapons"].is_array()) {
			for (const auto& choice : config["weapons"]) {
				const int definition = readInt(choice, "definition", 0);
				const int paintKit = readInt(choice, "paintKit", 0);
				const SkinEntry* skin = findSkinEntry(definition, paintKit, false, false);
				if (!skin) continue;
				g_weaponSelection[definition] = SelectedSkin{
					*skin, configWear(choice, *skin), readInt(choice, "seed", 0),
					readInt(choice, "statTrak", -1)
				};
			}
		}

		if (config.contains("knife") && config["knife"].is_object() &&
			readBool(config["knife"], "enabled", false)) {
			const auto& choice = config["knife"];
			const int definition = readInt(choice, "definition", 0);
			const int paintKit = readInt(choice, "paintKit", 0);
			const SkinEntry* skin = findSkinEntry(definition, paintKit, true, false);
			for (int i = 0; skin && i < static_cast<int>(kKnives.size()) - 1; ++i) {
				if (kKnives[i].definition != definition) continue;
				g_knifeSkin = SelectedSkin{ *skin, configWear(choice, *skin),
					readInt(choice, "seed", 0), readInt(choice, "statTrak", -1) };
				g_selectedKnife = i;
				g_knifeIndex = i;
				g_hasKnifeSkin = true;
				break;
			}
		}

		if (config.contains("gloves") && config["gloves"].is_object() &&
			readBool(config["gloves"], "enabled", false)) {
			const auto& choice = config["gloves"];
			const int definition = readInt(choice, "definition", 0);
			const int paintKit = readInt(choice, "paintKit", 0);
			const SkinEntry* skin = findSkinEntry(definition, paintKit, false, true);
			for (int i = 0; skin && i < static_cast<int>(kGloveTypes.size()); ++i) {
				if (kGloveTypes[i].first != definition) continue;
				g_gloveSkin = SelectedSkin{ *skin, configWear(choice, *skin),
					readInt(choice, "seed", 0), -1 };
				g_selectedGloveDefinition = definition;
				g_gloveIndex = i;
				g_hasGloveSkin = true;
				break;
			}
		}

		if (config.contains("agents") && config["agents"].is_object()) {
			const auto& agents = config["agents"];
			if (agents.contains("ct") && agents["ct"].is_string())
				g_agentCtModel = agents["ct"].get<std::string>();
			if (agents.contains("t") && agents["t"].is_string())
				g_agentTModel = agents["t"].get<std::string>();
			for (int i = 0; i < static_cast<int>(g_agents.size()); ++i) {
				if ((!g_agentCtModel.empty() && g_agents[i].model == g_agentCtModel) ||
					(!g_agentTModel.empty() && g_agents[i].model == g_agentTModel)) {
					g_agentIndex = i;
					break;
				}
			}
		}

		g_lastGlovePawn = 0;
		g_lastGlovePaint = -1;
		g_lastGloveSeed = -1;
		g_lastGloveWear = -1.0f;
		requestSkinRefresh();
	}

	void startCatalogLoad()
	{
		bool expected = false;
		if (!g_loadStarted.compare_exchange_strong(expected, true)) return;
		std::thread([] {
			try {
				auto catalog = SkinsCatalog::load();
				std::lock_guard<std::mutex> stateLock(g_stateMutex);
				if (!catalog.error.empty()) {
					g_error = std::move(catalog.error);
					g_catalogFailed.store(true, std::memory_order_release);
					return;
				}
				g_skins = std::move(catalog.skins);
				g_agents = std::move(catalog.agents);
				g_weaponSkinsByDefinition.clear();
				for (const auto& skin : g_skins) {
					if (!skin.knife && !skin.glove)
						g_weaponSkinsByDefinition[skin.weaponId].push_back(&skin);
				}
				for (auto& entry : g_weaponSkinsByDefinition) {
					auto& skins = entry.second;
					std::stable_sort(skins.begin(), skins.end(),
						[](const SkinEntry* a, const SkinEntry* b) { return a->rarity > b->rarity; });
				}
				g_catalogReady.store(true, std::memory_order_release);
			}
			catch (...) {
				std::lock_guard<std::mutex> stateLock(g_stateMutex);
				g_error = "Catalog loading failed.";
				g_catalogFailed.store(true, std::memory_order_release);
			}
		}).detach();
	}

	void initializeFunctions()
	{
		std::call_once(g_initializeFunctionsOnce, [] {
		if (!g_setItemAttribute)
			g_setItemAttribute = reinterpret_cast<SetItemAttributeFn>(M::patternScan("client",
				"40 53 48 83 EC 20 48 8B D9 48 81 C1 08 02 00 00"));
		if (!g_addStatTrakEntity)
			g_addStatTrakEntity = reinterpret_cast<AddStatTrakEntityFn>(M::patternScan("client",
				"48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC 40 01 00 00 48 8B DA"));
		if (!g_changeModel) {
			// Resolve the model setter from its call site; the rel32 target
			// is the function taking (entity, modelPath).
			const uintptr_t callSite = M::patternScan("client",
				"48 8D 15 ? ? ? ? 48 8B CB E8 ? ? ? ? 48 8B D7 48 8B CB");
			if (callSite) {
				const uintptr_t callInstruction = callSite + 10;
				const int32_t relativeTarget = *reinterpret_cast<const int32_t*>(callInstruction + 1);
				g_changeModel = reinterpret_cast<ChangeModelFn>(callInstruction + 5 + relativeTarget);
			}
		}
		if (!g_getViewModel)
			g_getViewModel = reinterpret_cast<GetViewModelFn>(M::patternScan("client",
				"40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 83 BB 88 03 00 00"));
		if (!g_getModelPath)
			g_getModelPath = reinterpret_cast<GetModelPathFn>(M::patternScan("client",
				"48 89 5C 24 10 56 48 83 EC 20 48 8B 1D ? ? ? ?"));
		if (!g_applyEconCustomization)
			g_applyEconCustomization = reinterpret_cast<ApplyEconCustomizationFn>(M::patternScan("client",
				"48 89 5C 24 08 57 48 83 EC 20 8B FA 48 8B D9 E8 ? ? ? ? 48 8B CB E8 ? ? ? ? 48 85 C0 74"));
		if (!g_updateSubclass)
			g_updateSubclass = reinterpret_cast<UpdateSubclassFn>(M::patternScan("client",
				"4C 8B DC 53 48 81 EC ? ? ? ? 48 8B 41"));
		if (!g_regenerateWeaponSkins)
			g_regenerateWeaponSkins = reinterpret_cast<RegenerateWeaponSkinsFn>(M::patternScan("client",
				"48 83 EC ? E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 8B 10"));
		if (!g_regenerateWeaponSkin && g_regenerateWeaponSkins) {
			g_regenerateWeaponSkin = reinterpret_cast<RegenerateWeaponSkinFn>(
				resolveRegenerateWeaponSkin(reinterpret_cast<uintptr_t>(g_regenerateWeaponSkins)));
		}
		if (!g_setMeshGroupMask)
			g_setMeshGroupMask = reinterpret_cast<SetMeshGroupMaskFn>(M::patternScan("client",
				"48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8D 99 ? ? ? ? 48 8B 71"));
		if (!g_invalidateItemDescription)
			g_invalidateItemDescription = reinterpret_cast<InvalidateItemDescriptionFn>(M::patternScan("client",
				"48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8D B9"));
		if (!g_setBodygroup)
			g_setBodygroup = reinterpret_cast<SetBodygroupFn>(M::patternScan("client",
				"85 D2 0F 88 ? ? ? ? 55 56 57 48 83 EC ? 41 8B E8 4C 89 B4 24 ? ? ? ? 8B FA 48 8B F1 E8 ? ? ? ?"));
		DebugConsole::once("skins.signature_status",
			"[skins] resolved set_attribute=0x%llX add_stattrak=0x%llX change_model=0x%llX viewmodel=0x%llX model_path=0x%llX apply_econ=0x%llX update_subclass=0x%llX regen_all=0x%llX regen_one=0x%llX mesh_group=0x%llX invalidate=0x%llX bodygroup=0x%llX",
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_setItemAttribute)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_addStatTrakEntity)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_changeModel)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_getViewModel)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_getModelPath)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_applyEconCustomization)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_updateSubclass)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_regenerateWeaponSkins)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_regenerateWeaponSkin)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_setMeshGroupMask)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_invalidateItemDescription)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_setBodygroup)));
		});
	}

	bool safeSetItemAttribute(void* item, const char* name, float value)
	{
		if (!g_setItemAttribute || !item || !name) return false;
		__try {
			g_setItemAttribute(item, name, value);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool safeSetStatTrakAttributes(void* item, int value)
	{
		const int countBits = value >= 0 ? value : -1;
		const int scoreTypeBits = value >= 0 ? 0 : -1;
		float countValue = 0.0f;
		float scoreTypeValue = 0.0f;
		std::memcpy(&countValue, &countBits, sizeof(countValue));
		std::memcpy(&scoreTypeValue, &scoreTypeBits, sizeof(scoreTypeValue));
		return safeSetItemAttribute(item, "kill eater", countValue) &&
			safeSetItemAttribute(item, "kill eater score type", scoreTypeValue);
	}

	bool safeAddStatTrakEntity(uintptr_t weapon, uintptr_t item)
	{
		if (!g_addStatTrakEntity || weapon <= 0x10000 || item <= 0x10000)
			return false;
		__try {
			g_addStatTrakEntity(reinterpret_cast<void*>(weapon), reinterpret_cast<void*>(item));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool safeReadItemDefinition(uintptr_t item, uint16_t* definition)
	{
		const std::ptrdiff_t definitionOffset = Offset::C_EconItemView::m_iItemDefinitionIndex;
		if (item <= 0x10000 || !definition || definitionOffset <= 0) return false;
		__try {
			*definition = *reinterpret_cast<uint16_t*>(item + definitionOffset);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool safeApplyLoadoutSkin(uintptr_t item, int targetDefinition, int quality,
		int paintKit, int seed, float wear, int statTrak)
	{
		const std::ptrdiff_t definitionOffset = Offset::C_EconItemView::m_iItemDefinitionIndex;
		const std::ptrdiff_t qualityOffset = Offset::C_EconItemView::m_iEntityQuality;
		const std::ptrdiff_t itemIdOffset = Offset::C_EconItemView::m_iItemID;
		const std::ptrdiff_t itemIdHighOffset = Offset::C_EconItemView::m_iItemIDHigh;
		const std::ptrdiff_t itemIdLowOffset = Offset::C_EconItemView::m_iItemIDLow;
		const std::ptrdiff_t initializedOffset = Offset::C_EconItemView::m_bInitialized;
		const std::ptrdiff_t disallowSocOffset = Offset::C_EconItemView::m_bDisallowSOC;
		if (!g_setItemAttribute || item <= 0x10000 || targetDefinition <= 0 || paintKit <= 0 ||
			definitionOffset <= 0 || qualityOffset <= 0 || itemIdOffset <= 0 || itemIdHighOffset <= 0 ||
			itemIdLowOffset <= 0 || initializedOffset <= 0 || disallowSocOffset <= 0)
			return false;

		const uint64_t fauxItemId = 0xF000000000000000ull | static_cast<uint64_t>(targetDefinition);
		__try {
			*reinterpret_cast<uint16_t*>(item + definitionOffset) = static_cast<uint16_t>(targetDefinition);
			if (quality >= 0)
				*reinterpret_cast<int*>(item + qualityOffset) = quality;
			*reinterpret_cast<uint64_t*>(item + itemIdOffset) = fauxItemId;
			*reinterpret_cast<uint32_t*>(item + itemIdHighOffset) = static_cast<uint32_t>(fauxItemId >> 32);
			*reinterpret_cast<uint32_t*>(item + itemIdLowOffset) = static_cast<uint32_t>(fauxItemId);
			*reinterpret_cast<uint8_t*>(item + initializedOffset) = 1;
			*reinterpret_cast<uint8_t*>(item + disallowSocOffset) = 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}

		return safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture prefab", static_cast<float>(paintKit)) &&
			safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture seed", static_cast<float>(seed)) &&
			safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture wear", wear) &&
			safeSetStatTrakAttributes(reinterpret_cast<void*>(item), statTrak);
	}

	bool isHudWeaponEntity(C_BaseEntity* candidate)
	{
		if (!candidate) return false;
		__try {
			const uintptr_t address = reinterpret_cast<uintptr_t>(candidate);
			const uintptr_t identity = *reinterpret_cast<const uintptr_t*>(address + 0x10);
			const uintptr_t classBinding = identity > 0x10000
				? *reinterpret_cast<const uintptr_t*>(identity + 0x8) : 0;
			const uintptr_t classInfo = classBinding > 0x10000
				? *reinterpret_cast<const uintptr_t*>(classBinding + 0x58) : 0;
			const char* className = classInfo > 0x10000
				? *reinterpret_cast<const char* const*>(classInfo + 0x8) : nullptr;
			if (className && std::strcmp(className, "C_CS2HudModelWeapon") == 0)
				return true;
			const char* name = reinterpret_cast<CEntityInstance*>(candidate)->designerName();
			return name && std::strstr(name, "hud_model_weapon") &&
				!std::strstr(name, "arms") && !std::strstr(name, "addon");
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool hudWeaponMatchesActiveDefinition(C_BaseEntity* candidate, uint16_t activeDefinition,
		uint32_t itemDefinitionOffset)
	{
		if (!candidate || activeDefinition == 0 || !itemDefinitionOffset || !isHudWeaponEntity(candidate)) return false;
		__try {
			const uintptr_t hudDefinitionAddress = reinterpret_cast<uintptr_t>(candidate) + itemDefinitionOffset;
			return *reinterpret_cast<const uint16_t*>(hudDefinitionAddress) == activeDefinition;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	C_BaseEntity* findHudModelWeapon(C_CSPlayerPawn* pawn, uintptr_t activeWeapon)
	{
		if (!pawn || !activeWeapon || !I::GameEntity || !I::GameEntity->Instance) return nullptr;
		__try {
			const uint32_t attributeManagerOffset = SchemaFinder::Get("C_EconEntity->m_AttributeManager");
			const uint32_t itemOffset = SchemaFinder::Get("C_AttributeContainer->m_Item");
			const uint32_t definitionOffset = SchemaFinder::Get("C_EconItemView->m_iItemDefinitionIndex");
			if (!attributeManagerOffset || !itemOffset || !definitionOffset) return nullptr;
			const uint32_t itemDefinitionOffset = attributeManagerOffset + itemOffset + definitionOffset;
			const uint16_t activeDefinition = *reinterpret_cast<const uint16_t*>(activeWeapon + itemDefinitionOffset);
			if (!activeDefinition) return nullptr;

			const uint32_t sceneOffset = SchemaFinder::Get("C_BaseEntity->m_pGameSceneNode");
			const uint32_t armsOffset = SchemaFinder::Get("C_CSPlayerPawn->m_hHudModelArms");
			const uint32_t childOffset = SchemaFinder::Get("CGameSceneNode->m_pChild");
			const uint32_t ownerOffset = SchemaFinder::Get("CGameSceneNode->m_pOwner");
			const uint32_t siblingOffset = SchemaFinder::Get("CGameSceneNode->m_pNextSibling");
			uint32_t attachmentOffset = SchemaFinder::Get("C_EconEntity->m_hViewmodelAttachment");
			if (!attachmentOffset)
				attachmentOffset = SchemaFinder::Get("C_CSWeaponBase->m_hViewmodelAttachment");
			if (!attachmentOffset)
				attachmentOffset = SchemaFinder::Get("C_BaseEntity->m_hViewmodelAttachment");
			DebugConsole::once("skins.hud_lookup_schema",
				"[skins] HUD lookup schema attachment=0x%X arms=0x%X",
				attachmentOffset, armsOffset);

			if (attachmentOffset && sceneOffset) {
				const uint32_t rawAttachment = *reinterpret_cast<uint32_t*>(activeWeapon + attachmentOffset);
				if (rawAttachment && rawAttachment != 0xFFFFFFFFu) {
					const CBaseHandle attachmentHandle(
						static_cast<int>(rawAttachment & 0x7FFFu), static_cast<int>(rawAttachment >> 15));
					C_BaseEntity* attachment = I::GameEntity->Instance->Get<C_BaseEntity>(attachmentHandle);
					if (attachment && reinterpret_cast<C_BaseEntity*>(attachment)->handle() == attachmentHandle &&
						hudWeaponMatchesActiveDefinition(attachment, activeDefinition, itemDefinitionOffset)) {
						const uintptr_t node = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(attachment) + sceneOffset);
						if (node > 0x10000) return attachment;
					}
				}
			}

			if (armsOffset && sceneOffset && childOffset && ownerOffset && siblingOffset) {
				const uint32_t rawHandle = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(pawn) + armsOffset);
				if (rawHandle && rawHandle != 0xFFFFFFFFu) {
					const CBaseHandle handle(static_cast<int>(rawHandle & 0x7FFFu), static_cast<int>(rawHandle >> 15));
					C_BaseEntity* arms = I::GameEntity->Instance->Get<C_BaseEntity>(handle);
					if (arms && reinterpret_cast<C_BaseEntity*>(arms)->handle() == handle) {
						const uintptr_t armsNode = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(arms) + sceneOffset);
						if (armsNode > 0x10000) {
							uintptr_t pending[128]{};
							uintptr_t visited[128]{};
							int pendingCount = 0;
							int visitedCount = 0;
							pending[pendingCount++] = *reinterpret_cast<uintptr_t*>(armsNode + childOffset);
							while (pendingCount > 0 && visitedCount < 128) {
								const uintptr_t node = pending[--pendingCount];
								if (node <= 0x10000) continue;
								bool seen = false;
								for (int i = 0; i < visitedCount; ++i) {
									if (visited[i] == node) { seen = true; break; }
								}
								if (seen) continue;
								visited[visitedCount++] = node;

								const uintptr_t child = *reinterpret_cast<uintptr_t*>(node + childOffset);
								const uintptr_t sibling = *reinterpret_cast<uintptr_t*>(node + siblingOffset);
								if (child > 0x10000 && pendingCount < 128) pending[pendingCount++] = child;
								if (sibling > 0x10000 && pendingCount < 128) pending[pendingCount++] = sibling;

								const uintptr_t owner = *reinterpret_cast<uintptr_t*>(node + ownerOffset);
								if (owner <= 0x10000) continue;
								auto* candidate = reinterpret_cast<C_BaseEntity*>(owner);
								if (!hudWeaponMatchesActiveDefinition(candidate, activeDefinition, itemDefinitionOffset)) continue;
								const uintptr_t candidateNode = *reinterpret_cast<uintptr_t*>(owner + sceneOffset);
								if (candidateNode > 0x10000) return candidate;
							}
						}
					}
				}
			}

			if (sceneOffset && I::GameEntity->Instance) {
				const uint32_t entityOwnerOffset = SchemaFinder::Get("C_BaseEntity->m_hOwnerEntity");
				const int highest = (std::min)(I::GameEntity->Instance->GetHighestEntityIndex(), 0x7FFE);
				int candidateCount = 0;
				int matchingCandidateCount = 0;
				for (int i = 1; i <= highest; ++i) {
					C_BaseEntity* candidate = I::GameEntity->Instance->Get<C_BaseEntity>(i);
					if (!candidate || !isHudWeaponEntity(candidate)) continue;
					const CBaseHandle candidateHandle = candidate->handle();
					if (!candidateHandle.valid() || candidateHandle.index() != i) continue;
					const uintptr_t address = reinterpret_cast<uintptr_t>(candidate);
					const uintptr_t node = *reinterpret_cast<uintptr_t*>(address + sceneOffset);
					if (node <= 0x10000) continue;
					++candidateCount;
					if (!hudWeaponMatchesActiveDefinition(candidate, activeDefinition, itemDefinitionOffset)) continue;
					++matchingCandidateCount;

					if (entityOwnerOffset) {
						const uint32_t rawOwner = *reinterpret_cast<uint32_t*>(address + entityOwnerOffset);
						if (rawOwner && rawOwner != 0xFFFFFFFFu) {
							const CBaseHandle ownerHandle(
								static_cast<int>(rawOwner & 0x7FFFu), static_cast<int>(rawOwner >> 15));
							C_BaseEntity* owner = I::GameEntity->Instance->Get<C_BaseEntity>(ownerHandle);
							if (owner && owner == reinterpret_cast<C_BaseEntity*>(pawn) &&
								owner->handle() == ownerHandle) {
								DebugConsole::once("skins.hud_entity_scan_owner",
									"[skins] HUD weapon resolved by entity scan index=%d owner=pawn",
									i);
								return candidate;
							}
						}
					}
				}
				if (matchingCandidateCount == 1) {
					for (int i = 1; i <= highest; ++i) {
						C_BaseEntity* candidate = I::GameEntity->Instance->Get<C_BaseEntity>(i);
						if (!candidate || !hudWeaponMatchesActiveDefinition(candidate, activeDefinition, itemDefinitionOffset)) continue;
						const uintptr_t node = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(candidate) + sceneOffset);
						if (node > 0x10000) return candidate;
					}
				}
				if (matchingCandidateCount > 1) {
					DebugConsole::rateLimited("skins.hud_entity_ambiguous",
						"[skins] HUD weapon lookup ambiguous: matches=%d active_definition=%u",
						matchingCandidateCount, static_cast<unsigned int>(activeDefinition));
				}
				if (candidateCount == 0) {
					DebugConsole::rateLimited("skins.hud_entity_scan_miss",
						"[skins] HUD entity scan found no candidates (highest=%d owner_offset=0x%X)",
						highest, entityOwnerOffset);
				} else if (matchingCandidateCount == 0) {
					DebugConsole::once("skins.hud_entity_scan_first",
						"[skins] HUD weapon lookup skipped unrelated candidates=%d",
						candidateCount);
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		return nullptr;
	}

	bool setViewModelMeshGroup(C_CSPlayerPawn* pawn, uintptr_t activeWeapon, bool legacy)
	{
		initializeFunctions();
		if (!pawn || !activeWeapon || !g_setMeshGroupMask) return false;
		const uint64_t mask = legacy ? 2ull : 1ull;
		__try {
			C_BaseEntity* viewModelWeapon = findHudModelWeapon(pawn, activeWeapon);
			if (!viewModelWeapon) return false;
			const uint32_t sceneOffset = SchemaFinder::Get("C_BaseEntity->m_pGameSceneNode");
			if (!sceneOffset) return false;
			const uintptr_t sceneNode = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(viewModelWeapon) + sceneOffset);
			if (sceneNode <= 0x10000) return false;
			g_setMeshGroupMask(reinterpret_cast<void*>(sceneNode), mask);
			DebugConsole::once("skins.hud_mesh_mask_applied",
				"[skins] HUD weapon mesh mask applied target=0x%llX mask=0x%llX",
				static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(viewModelWeapon)),
				static_cast<unsigned long long>(mask));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool setViewModelWeaponModel(C_CSPlayerPawn* pawn, uintptr_t activeWeapon, C_BaseEntity* hudWeapon)
	{
		initializeFunctions();
		if (!pawn || !activeWeapon || !hudWeapon || !g_changeModel || !g_getModelPath) return false;
		__try {
			const uintptr_t item = activeWeapon + Offset::C_EconEntity::m_AttributeManager +
				Offset::C_AttributeContainer::m_Item;
			const char* model = g_getModelPath(reinterpret_cast<void*>(item));
			if (!model || !*model) return false;
			g_changeModel(hudWeapon, model);
			DebugConsole::rateLimited("skins.hud_model_synced",
				"[skins] active HUD model synced weapon=0x%llX hud=0x%llX model=%s",
				static_cast<unsigned long long>(activeWeapon),
				static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hudWeapon)), model);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			DebugConsole::rateLimited("skins.hud_model_sync_exception",
				"[skins] active HUD model sync raised an exception");
			return false;
		}
	}

	bool rebuildPaint(uintptr_t weapon, C_CSPlayerPawn* pawn, bool activeWeapon, bool legacy)
	{
		initializeFunctions();
		if (!weapon || !g_applyEconCustomization || !g_updateSubclass || !g_regenerateWeaponSkin || !g_setMeshGroupMask) {
			DebugConsole::once("skins.rebuild_prerequisite_missing",
				"[skins] rebuild prerequisites missing apply_econ=%d update_subclass=%d regen_one=%d mesh=%d",
				g_applyEconCustomization ? 1 : 0, g_updateSubclass ? 1 : 0,
				g_regenerateWeaponSkin ? 1 : 0, g_setMeshGroupMask ? 1 : 0);
			return false;
		}

		const uint64_t meshGroup = legacy ? 2ull : 1ull;
		const char* failingStep = "ApplyEconCustomization";
		__try {
			g_applyEconCustomization(reinterpret_cast<void*>(weapon), true);
			failingStep = "weapon vfunc 113";
			M::vfunc<void, 113>(reinterpret_cast<void*>(weapon), true);
			failingStep = "weapon vfunc 200";
			M::vfunc<void, 200>(reinterpret_cast<void*>(weapon));
			failingStep = "weapon vfunc 105";
			M::vfunc<void, 105>(reinterpret_cast<void*>(weapon), true);
			failingStep = "UpdateSubclass";
			g_updateSubclass(reinterpret_cast<void*>(weapon));
			failingStep = "RegenerateWeaponSkin";
			// The game's global updater reaches this routine and calls
			// it as (weapon, false); true is not a generic force-refresh flag here.
			g_regenerateWeaponSkin(reinterpret_cast<void*>(weapon), false);
			failingStep = "weapon mesh mask";
			const uintptr_t sceneNode = *reinterpret_cast<uintptr_t*>(weapon + Offset::C_BaseEntity::m_pGameSceneNode);
			if (sceneNode) g_setMeshGroupMask(reinterpret_cast<void*>(sceneNode), meshGroup);
			if (activeWeapon && !setViewModelMeshGroup(pawn, weapon, legacy))
				DebugConsole::rateLimited("skins.hud_model_wait", "[skins] HUD mesh target not ready after skin regeneration");
			DebugConsole::logf("[skins] weapon appearance rebuild complete weapon=0x%llX regen_one=0x%llX mesh_group=0x%llX",
				static_cast<unsigned long long>(weapon),
				static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_regenerateWeaponSkin)),
				static_cast<unsigned long long>(meshGroup));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			DebugConsole::rateLimited("skins.rebuild_exception", "[skins] rebuild raised during %s", failingStep);
			return false;
		}
	}

	void retainCustomMaterialAfterPrecache(uintptr_t item)
	{
		if (!item) return;
		__try {
			*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bRestoreCustomMaterialAfterPrecache) = 1;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	uint32_t knifeSubclassToken(int definition)
	{
		constexpr uint32_t multiplier = 0x5bd1e995;
		constexpr uint32_t seed = 0x31415926;
		const std::string value = std::to_string(definition);
		uint32_t hash = seed ^ static_cast<uint32_t>(value.size());
		if (value.size() >= 3)
			hash ^= static_cast<uint32_t>(std::tolower(static_cast<unsigned char>(value[2]))) << 16;
		if (value.size() >= 2)
			hash ^= static_cast<uint32_t>(std::tolower(static_cast<unsigned char>(value[1]))) << 8;
		if (!value.empty())
			hash ^= static_cast<uint32_t>(std::tolower(static_cast<unsigned char>(value[0])));
		if (!value.empty()) hash *= multiplier;
		hash ^= hash >> 13;
		hash *= multiplier;
		hash ^= hash >> 15;
		return hash;
	}

	uint32_t localAccountId(C_CSPlayerPawn* pawn)
	{
		if (!pawn || !I::GameEntity || !I::GameEntity->Instance) return 0;
		__try {
			const uintptr_t pawnAddress = reinterpret_cast<uintptr_t>(pawn);
			const uint32_t rawHandle = *reinterpret_cast<uint32_t*>(pawnAddress + Offset::C_BasePlayerPawn::m_hController);
			if (rawHandle == 0xFFFFFFFFu) return 0;
			const CBaseHandle handle(static_cast<int>(rawHandle & 0x7FFFu), static_cast<int>(rawHandle >> 15));
			C_BaseEntity* controller = I::GameEntity->Instance->Get<C_BaseEntity>(handle);
			if (!controller) return 0;
			const uint64_t steamId = *reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(controller) + Offset::CBasePlayerController::m_steamID);
			return static_cast<uint32_t>(steamId & 0xFFFFFFFFu);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return 0;
		}
	}

	bool safeChangeModel(void* entity, const char* model)
	{
		if (!g_changeModel || !entity || !model) return false;
		__try {
			g_changeModel(entity, model);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool safeChangeItemModel(void* weapon, void* item, C_CSPlayerPawn* pawn, bool activeWeapon)
	{
		if (!weapon || !item || !g_changeModel || !g_getViewModel || !g_getModelPath) return false;
		__try {
			g_getViewModel(weapon);
			const char* model = g_getModelPath(item);
			if (!model || !*model) return false;
			g_changeModel(weapon, model);
			if (activeWeapon && pawn) {
				if (C_BaseEntity* hudWeapon = findHudModelWeapon(pawn, reinterpret_cast<uintptr_t>(weapon));
					hudWeapon && hudWeapon != reinterpret_cast<C_BaseEntity*>(weapon))
					g_changeModel(hudWeapon, model);
			}
			DebugConsole::once("skins.knife_model_path",
				"[skins] knife model setter=0x%llX item_path=%s",
				static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_changeModel)), model);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	ImU32 rarityColor(int rarity)
	{
		switch (rarity) {
		case 7: return IM_COL32(220, 62, 62, 255);
		case 6: return IM_COL32(235, 75, 75, 255);
		case 5: return IM_COL32(211, 44, 230, 255);
		case 4: return IM_COL32(136, 71, 255, 255);
		case 3: return IM_COL32(75, 105, 255, 255);
		case 2: return IM_COL32(75, 105, 255, 255);
		default: return IM_COL32(145, 145, 155, 255);
		}
	}

	bool matchesSearch(const std::string& name)
	{
		static std::string previousSearch;
		static std::string normalizedSearch;
		const std::string search = activeSearch();
		if (previousSearch != search) {
			previousSearch = search;
			normalizedSearch = previousSearch;
			std::transform(normalizedSearch.begin(), normalizedSearch.end(), normalizedSearch.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		}
		return normalizedSearch.empty() || name.find(normalizedSearch) != std::string::npos;
	}

	void inputCategorySearch(const char* id, const char* hint)
	{
		const std::string before = activeSearch();
		ImGui::InputTextWithHint(id, hint, activeSearch(), static_cast<size_t>(96));
		if (before != activeSearch()) activeSkinIndex() = -1;
	}

	bool isKnifeDefinition(int definition)
	{
		return definition == 42 || definition == 59 || (definition >= 500 && definition <= 526);
	}

	std::string defaultAgentModel(int team)
	{
		for (const auto& agent : g_agents) {
			if (agent.name == "Default" && agent.counterTerrorist == (team == 3))
				return agent.model;
		}
		return team == 3
			? "agents/models/ctm_sas/ctm_sas.vmdl"
			: "agents/models/tm_phoenix/tm_phoenix.vmdl";
	}

	const KnifeEntry* selectedKnife()
	{
		return g_selectedKnife >= 0 && g_selectedKnife < static_cast<int>(kKnives.size()) - 1
			? &kKnives[g_selectedKnife] : nullptr;
	}

	void drawSkinCard(const SkinEntry& skin, int index)
	{
		const ImVec2 size(148.0f, 124.0f);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::PushID(index);
		if (ImGui::InvisibleButton("skin-card", size)) activeSkinIndex() = index;
		const bool selected = activeSkinIndex() == index;
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 cardMax = ImVec2(p.x + size.x, p.y + size.y);
		draw->AddRectFilled(p, cardMax, IM_COL32(21, 20, 27, 255), 9.0f);
		draw->AddRect(p, cardMax, selected ? IM_COL32(169, 112, 255, 255) : rarityColor(skin.rarity), 9.0f, 0, selected ? 2.0f : 1.2f);
		draw->AddCircleFilled(ImVec2(cardMax.x - 10.0f, p.y + 10.0f), 3.0f, rarityColor(skin.rarity));
		const ImVec2 previewMin(p.x + 7.0f, p.y + 7.0f);
		const ImVec2 previewMax(cardMax.x - 7.0f, cardMax.y - 29.0f);
		const PreviewLoadState previewState = drawPreviewImage(draw, skin.image, previewMin, previewMax);
		drawPreviewStatus(draw, skin.image, previewState, previewMin, previewMax);
		std::string title = skin.name;
		constexpr float titleWidth = 134.0f;
		if (ImGui::CalcTextSize(title.c_str()).x > titleWidth) {
			while (!title.empty() && ImGui::CalcTextSize((title + "...").c_str()).x > titleWidth) {
				size_t cut = title.size() - 1;
				while (cut > 0 && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80) --cut;
				title.erase(cut);
			}
			if (!title.empty()) title += "...";
		}
		draw->PushClipRect(p, cardMax, true);
		draw->AddText(ImVec2(p.x + 7.0f, cardMax.y - 23.0f), IM_COL32(210, 207, 216, 255), title.c_str());
		draw->PopClipRect();
		ImGui::PopID();
	}

	void drawGallery(const std::vector<const SkinEntry*>& visible)
	{
		ImGui::BeginChild("SkinGallery", ImVec2(0.0f, 300.0f), true);
		const float cardWidth = 148.0f;
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const int columns = (std::max)(1, static_cast<int>((ImGui::GetContentRegionAvail().x + gap) / (cardWidth + gap)));
		for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
			if (i % columns != 0) ImGui::SameLine();
			drawSkinCard(*visible[i], i);
		}
		if (visible.empty()) ImGui::TextDisabled("No matching items.");
		ImGui::EndChild();
	}

	void drawSkinSelectionState(const SkinEntry& skin, const SelectedSkin* applied)
	{
		const bool samePaint = applied && applied->skin.paintKit == skin.paintKit;
		const bool sameSettings = samePaint && applied->wear == activeWear()
			&& applied->seed == activeSeed() && applied->statTrak == activeStatTrak();
		if (sameSettings) {
			if (ImGui::GetTime() < g_skinAppliedFeedbackUntil[static_cast<size_t>(g_category)])
				ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.48f, 1.0f), "Applied with current settings");
		} else if (samePaint) {
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.30f, 1.0f), "Applied finish; changed settings are pending Apply");
		} else if (applied) {
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.30f, 1.0f), "Preview only; another finish is currently applied");
		} else {
			ImGui::TextDisabled("Preview only; click Apply to equip this finish");
		}
	}

	void renderWeaponCategory()
	{
		static constexpr int weaponIds[] = {1,2,3,4,7,8,9,10,11,13,14,16,17,19,23,24,25,26,27,28,29,30,31,32,33,34,35,36,38,39,40,60,61,63,64};
		ImGui::TextUnformatted("Weapon");
		if (ImGui::BeginCombo("##weapon", weaponName(g_weaponId))) {
			for (int id : weaponIds) if (ImGui::Selectable(weaponName(id), id == g_weaponId)) { g_weaponId = id; activeSkinIndex() = -1; }
			ImGui::EndCombo();
		}
		inputCategorySearch("##skin-search", "Search finishes");
		std::vector<const SkinEntry*> visible;
		const auto weaponSkins = g_weaponSkinsByDefinition.find(g_weaponId);
		if (weaponSkins != g_weaponSkinsByDefinition.end()) {
			for (const SkinEntry* skin : weaponSkins->second)
				if (matchesSearch(skin->searchName)) visible.push_back(skin);
		}
		if (activeSkinIndex() < 0) {
			const auto selection = g_weaponSelection.find(g_weaponId);
			if (selection != g_weaponSelection.end()) {
				for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
					if (visible[i]->paintKit != selection->second.skin.paintKit) continue;
					activeSkinIndex() = i;
					activeWear() = selection->second.wear;
					activeSeed() = selection->second.seed;
					activeStatTrak() = selection->second.statTrak;
					break;
				}
			}
		}
		drawGallery(visible);
		if (activeSkinIndex() >= 0 && activeSkinIndex() < static_cast<int>(visible.size())) {
			const SkinEntry& skin = *visible[activeSkinIndex()];
			ImGui::TextColored(ImColor(rarityColor(skin.rarity)), "%s", skin.name.c_str());
			const auto selection = g_weaponSelection.find(g_weaponId);
			drawSkinSelectionState(skin, selection == g_weaponSelection.end() ? nullptr : &selection->second);
			ImGui::SliderFloat("Wear", &activeWear(), skin.minWear, (std::max)(skin.minWear + 0.001f, skin.maxWear), "%.4f");
			ImGui::InputInt("Seed", &activeSeed());
			ImGui::InputInt("StatTrak", &activeStatTrak());
			if (ImGui::Button("Apply to this weapon")) {
				g_weaponSelection[g_weaponId] = SelectedSkin{ skin, activeWear(), activeSeed(), activeStatTrak() };
				g_skinAppliedFeedbackUntil[static_cast<size_t>(g_category)] = ImGui::GetTime() + 5.0;
				requestSkinRefresh();
				DebugConsole::rateLimited("skins.weapon_selection", "[skins] selected weapon=%d paint=%d legacy=%d",
					g_weaponId, skin.paintKit, skin.legacyModel ? 1 : 0);
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear applied skin")) {
				g_weaponSelection.erase(g_weaponId);
				activeSkinIndex() = -1;
				requestSkinRefresh();
			}
		}
	}

	void renderKnifeCategory()
	{
		if (ImGui::BeginCombo("Knife model", kKnives[g_knifeIndex].name)) {
			for (int i = 0; i < static_cast<int>(kKnives.size()) - 1; ++i)
				if (ImGui::Selectable(kKnives[i].name, i == g_knifeIndex)) {
					g_knifeIndex = i;
					activeSkinIndex() = -1;
					activeStatTrak() = g_hasKnifeSkin && g_selectedKnife == i ? g_knifeSkin.statTrak : -1;
				}
			ImGui::EndCombo();
		}
		inputCategorySearch("##knife-search", "Search knife finishes");
		const std::string modelName = kKnives[g_knifeIndex].name;
		std::vector<const SkinEntry*> visible;
		for (const auto& skin : g_skins) if (skin.knife && skin.name.find(modelName) != std::string::npos && matchesSearch(skin.searchName)) visible.push_back(&skin);
		if (activeSkinIndex() < 0 && g_hasKnifeSkin && g_selectedKnife == g_knifeIndex) {
			for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
				if (visible[i]->paintKit != g_knifeSkin.skin.paintKit) continue;
				activeSkinIndex() = i;
				activeWear() = g_knifeSkin.wear;
				activeSeed() = g_knifeSkin.seed;
				activeStatTrak() = g_knifeSkin.statTrak;
				break;
			}
		}
		drawGallery(visible);
		if (activeSkinIndex() >= 0 && activeSkinIndex() < static_cast<int>(visible.size())) {
			const SkinEntry& skin = *visible[activeSkinIndex()];
			ImGui::TextColored(ImColor(rarityColor(skin.rarity)), "%s", skin.name.c_str());
			drawSkinSelectionState(skin,
				g_hasKnifeSkin && g_selectedKnife == g_knifeIndex ? &g_knifeSkin : nullptr);
			ImGui::SliderFloat("Wear", &activeWear(), skin.minWear, (std::max)(skin.minWear + 0.001f, skin.maxWear), "%.4f");
			ImGui::InputInt("Seed", &activeSeed());
			ImGui::InputInt("StatTrak", &activeStatTrak());
			if (activeStatTrak() < -1) activeStatTrak() = -1;
			if (ImGui::Button("Apply knife")) {
				g_knifeSkin = SelectedSkin{ skin, activeWear(), activeSeed(), activeStatTrak() };
				g_hasKnifeSkin = true;
				g_selectedKnife = g_knifeIndex;
				g_skinAppliedFeedbackUntil[static_cast<size_t>(g_category)] = ImGui::GetTime() + 5.0;
				requestSkinRefresh();
				DebugConsole::rateLimited("skins.knife_selection", "[skins] selected knife definition=%d paint=%d legacy=%d name=%s",
					kKnives[g_knifeIndex].definition, skin.paintKit, skin.legacyModel ? 1 : 0, skin.name.c_str());
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear applied knife skin")) {
				g_hasKnifeSkin = false;
				g_selectedKnife = -1;
				activeSkinIndex() = -1;
				requestSkinRefresh();
			}
		}
	}

	void renderGloveCategory()
	{
		if (ImGui::BeginCombo("Glove model", kGloveTypes[g_gloveIndex].second)) {
			for (int i = 0; i < static_cast<int>(kGloveTypes.size()); ++i)
				if (ImGui::Selectable(kGloveTypes[i].second, i == g_gloveIndex)) { g_gloveIndex = i; activeSkinIndex() = -1; }
			ImGui::EndCombo();
		}
		inputCategorySearch("##glove-search", "Search glove finishes");
		std::vector<const SkinEntry*> visible;
		for (const auto& skin : g_skins) if (skin.glove && skin.weaponName.find(kGloveTypes[g_gloveIndex].second) != std::string::npos && matchesSearch(skin.searchName)) visible.push_back(&skin);
		if (activeSkinIndex() < 0 && g_hasGloveSkin && g_selectedGloveDefinition == kGloveTypes[g_gloveIndex].first) {
			for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
				if (visible[i]->paintKit != g_gloveSkin.skin.paintKit) continue;
				activeSkinIndex() = i;
				activeWear() = g_gloveSkin.wear;
				activeSeed() = g_gloveSkin.seed;
				break;
			}
		}
		drawGallery(visible);
		if (activeSkinIndex() >= 0 && activeSkinIndex() < static_cast<int>(visible.size())) {
			const SkinEntry& skin = *visible[activeSkinIndex()];
			ImGui::TextColored(ImColor(rarityColor(skin.rarity)), "%s", skin.name.c_str());
			drawSkinSelectionState(skin,
				g_hasGloveSkin && g_selectedGloveDefinition == kGloveTypes[g_gloveIndex].first ? &g_gloveSkin : nullptr);
			ImGui::SliderFloat("Wear", &activeWear(), skin.minWear, (std::max)(skin.minWear + 0.001f, skin.maxWear), "%.4f");
			ImGui::InputInt("Seed", &activeSeed());
			const bool selectionChanged = !g_hasGloveSkin
				|| g_selectedGloveDefinition != kGloveTypes[g_gloveIndex].first
				|| g_gloveSkin.skin.paintKit != skin.paintKit
				|| g_gloveSkin.wear != activeWear()
				|| g_gloveSkin.seed != activeSeed();
			g_gloveSkin = SelectedSkin{ skin, activeWear(), activeSeed(), -1 };
			g_selectedGloveDefinition = kGloveTypes[g_gloveIndex].first;
			g_hasGloveSkin = true;
			if (selectionChanged)
				g_skinAppliedFeedbackUntil[static_cast<size_t>(g_category)] = ImGui::GetTime() + 5.0;
		}
	}

	void renderAgentCategory()
	{
		inputCategorySearch("##agent-search", "Search agents");
		ImGui::BeginChild("AgentCards", ImVec2(0.0f, 360.0f), true);
		const float cardWidth = 190.0f;
		const float cardHeight = 176.0f;
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const int columns = (std::max)(1, static_cast<int>((ImGui::GetContentRegionAvail().x + gap) / (cardWidth + gap)));
		int shown = 0;
		for (int i = 0; i < static_cast<int>(g_agents.size()); ++i) {
			const auto& agent = g_agents[i];
			if (!matchesSearch(agent.searchName)) continue;
			if (shown % columns) ImGui::SameLine();
			ImGui::PushID(i);
			const ImVec2 cardMin = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("agent-card", ImVec2(cardWidth, cardHeight))) {
				g_agentIndex = i;
				if (agent.counterTerrorist) g_agentCtModel = agent.model;
				else g_agentTModel = agent.model;
			}
			const std::string& assignedModel = agent.counterTerrorist ? g_agentCtModel : g_agentTModel;
			const bool selected = g_agentIndex == i || assignedModel == agent.model;
			const ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + cardHeight);
			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->AddRectFilled(cardMin, cardMax, IM_COL32(21, 20, 27, 255), 9.0f);
			draw->AddRect(cardMin, cardMax, selected ? IM_COL32(169, 112, 255, 255) : IM_COL32(68, 66, 78, 255),
				9.0f, 0, selected ? 2.0f : 1.0f);
			const ImVec2 previewMin(cardMin.x + 7.0f, cardMin.y + 7.0f);
			const ImVec2 previewMax(cardMax.x - 7.0f, cardMax.y - 30.0f);
			const PreviewLoadState previewState = drawPreviewImage(draw, agent.image, previewMin, previewMax);
			drawPreviewStatus(draw, agent.image, previewState, previewMin, previewMax);
			const std::string title = agent.name.size() > 27 ? agent.name.substr(0, 24) + "..." : agent.name;
			draw->AddText(ImVec2(cardMin.x + 7.0f, cardMax.y - 21.0f), IM_COL32(220, 218, 228, 255), title.c_str());
			ImGui::PopID();
			++shown;
		}
		ImGui::EndChild();
		if (g_agentIndex >= 0 && g_agentIndex < static_cast<int>(g_agents.size())) {
			const auto& agent = g_agents[g_agentIndex];
			const std::string& assignedModel = agent.counterTerrorist ? g_agentCtModel : g_agentTModel;
			ImGui::TextColored(ImVec4(0.78f, 0.63f, 1.0f, 1.0f), "%s - %s", agent.name.c_str(), agent.team.c_str());
			ImGui::TextDisabled("%s", assignedModel == agent.model ? "Assigned to this team" : "Preview only");
			if (ImGui::Button("Clear agents")) {
				g_agentCtModel.clear();
				g_agentTModel.clear();
				g_agentIndex = -1;
			}
		}
	}

	void captureWeaponBaseline(uintptr_t weapon, uintptr_t item)
	{
		if (g_weaponBaselines.find(weapon) != g_weaponBaselines.end()) return;
		g_weaponBaselines.emplace(weapon, WeaponBaseline{
			*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackPaintKit),
			*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackSeed),
			*reinterpret_cast<float*>(weapon + Offset::C_EconEntity::m_flFallbackWear),
			*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackStatTrak),
			*reinterpret_cast<uint64_t*>(item + Offset::C_EconItemView::m_iItemID),
			*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iItemIDHigh),
			*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iItemIDLow),
			*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iAccountID),
			*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bInitialized),
			*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bDisallowSOC),
			*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bRestoreCustomMaterialAfterPrecache),
			*reinterpret_cast<uint16_t*>(item + Offset::C_EconItemView::m_iItemDefinitionIndex),
			*reinterpret_cast<int*>(item + Offset::C_EconItemView::m_iEntityQuality),
			*reinterpret_cast<uint32_t*>(weapon + Offset::C_BaseEntity::m_nSubclassID)
		});
	}

	bool legacyModelForPaintKit(int paintKit)
	{
		for (const SkinEntry& skin : g_skins)
			if (skin.paintKit == paintKit) return skin.legacyModel;
		return false;
	}

	bool restoreWeapon(uintptr_t weapon, uintptr_t item, C_CSPlayerPawn* pawn, bool activeWeapon,
		bool restoreDefinition = false)
	{
		initializeFunctions();
		if (!g_setMeshGroupMask || !g_applyEconCustomization || !g_updateSubclass || !g_regenerateWeaponSkin ||
			(!restoreDefinition && !g_setItemAttribute) ||
			(restoreDefinition && (!g_getViewModel || !g_getModelPath || !g_changeModel)))
			return false;

		const auto baseline = g_weaponBaselines.find(weapon);
		if (baseline == g_weaponBaselines.end()) return false;
		const WeaponBaseline saved = baseline->second;
		*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackPaintKit) = saved.paintKit;
		*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackSeed) = saved.seed;
		*reinterpret_cast<float*>(weapon + Offset::C_EconEntity::m_flFallbackWear) = saved.wear;
		*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackStatTrak) = saved.statTrak;
		*reinterpret_cast<uint64_t*>(item + Offset::C_EconItemView::m_iItemID) = saved.itemId;
		*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iItemIDHigh) = saved.itemIdHigh;
		*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iItemIDLow) = saved.itemIdLow;
		*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iAccountID) = saved.accountId;
		*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bInitialized) = saved.initialized;
		*reinterpret_cast<int*>(item + Offset::C_EconItemView::m_iEntityQuality) = saved.quality;
		*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bDisallowSOC) = 0;
		if (!safeSetStatTrakAttributes(reinterpret_cast<void*>(item), saved.statTrak)) return false;
		if (saved.statTrak >= 0) safeAddStatTrakEntity(weapon, item);
		if (restoreDefinition) {
			*reinterpret_cast<uint16_t*>(item + Offset::C_EconItemView::m_iItemDefinitionIndex) = saved.definition;
			*reinterpret_cast<uint32_t*>(weapon + Offset::C_BaseEntity::m_nSubclassID) = saved.subclass;
			if (!safeChangeItemModel(reinterpret_cast<void*>(weapon), reinterpret_cast<void*>(item), pawn, activeWeapon))
				return false;
		} else {
			const bool attributesRestored =
				safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture prefab", static_cast<float>(saved.paintKit)) &&
				safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture seed", static_cast<float>(saved.seed)) &&
				safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture wear", saved.wear);
			if (!attributesRestored) return false;
		}
		if (!rebuildPaint(weapon, pawn, activeWeapon, legacyModelForPaintKit(saved.paintKit)))
			return false;
		*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bDisallowSOC) = saved.disallowSoc;
		*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bRestoreCustomMaterialAfterPrecache) = saved.restoreCustomMaterial;
		if (g_invalidateItemDescription) {
			__try { g_invalidateItemDescription(reinterpret_cast<void*>(item)); }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}
		g_weaponBaselines.erase(baseline);
		g_weaponHandles.erase(weapon);
		g_weaponSkinRegenerationPending = true;
		return true;
	}

	bool applyWeapon(uintptr_t weapon, uintptr_t item, uint32_t itemHandle, const SelectedSkin& selection, uint32_t accountId,
		C_CSPlayerPawn* pawn, bool activeWeapon, bool updateKnifeModel = false)
	{
		initializeFunctions();
		const SkinEntry& skin = selection.skin;
		if (!g_setMeshGroupMask || !g_applyEconCustomization || !g_updateSubclass ||
			!g_regenerateWeaponSkin || !g_setItemAttribute ||
			(updateKnifeModel && (!g_getViewModel || !g_getModelPath || !g_changeModel))) {
			DebugConsole::once("skins.apply_prerequisite_missing",
				"[skins] apply prerequisites missing mesh=%d apply_econ=%d update_subclass=%d regen_one=%d attribute=%d model=%d",
				g_setMeshGroupMask ? 1 : 0, g_applyEconCustomization ? 1 : 0,
				g_updateSubclass ? 1 : 0, g_regenerateWeaponSkin ? 1 : 0, g_setItemAttribute ? 1 : 0,
				(!updateKnifeModel || (g_getViewModel && g_getModelPath && g_changeModel)) ? 1 : 0);
			return false;
		}

		captureWeaponBaseline(weapon, item);
		const auto baseline = g_weaponBaselines.find(weapon);
		if (baseline == g_weaponBaselines.end()) return false;
		*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bDisallowSOC) = 0;
		const uint64_t fauxItemId = makeFauxItemId(updateKnifeModel ? kKnifeFauxItemHandle : itemHandle);
		*reinterpret_cast<uint64_t*>(item + Offset::C_EconItemView::m_iItemID) = fauxItemId;
		*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iItemIDHigh) = static_cast<uint32_t>(fauxItemId >> 32);
		*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iItemIDLow) = static_cast<uint32_t>(fauxItemId);
		*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iAccountID) = accountId;
		*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bInitialized) = 1;
		*reinterpret_cast<int*>(item + Offset::C_EconItemView::m_iEntityQuality) =
			selection.statTrak >= 0 ? 3 : baseline->second.quality;
		*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackPaintKit) = skin.paintKit;
		*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackSeed) = selection.seed;
		*reinterpret_cast<float*>(weapon + Offset::C_EconEntity::m_flFallbackWear) = selection.wear;
		*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackStatTrak) = selection.statTrak;

		const bool attributesSet = safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture prefab", static_cast<float>(skin.paintKit)) &&
			safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture seed", static_cast<float>(selection.seed)) &&
			safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture wear", selection.wear) &&
			safeSetStatTrakAttributes(reinterpret_cast<void*>(item), selection.statTrak);
		if (!attributesSet) {
			DebugConsole::rateLimited("skins.attributes_set_failed", "[skins] item paint attributes failed weapon=0x%llX paint=%d seed=%d",
				static_cast<unsigned long long>(weapon), skin.paintKit, selection.seed);
			return false;
		}
		const bool statTrakEntityAdded = selection.statTrak < 0 || safeAddStatTrakEntity(weapon, item);
		if (updateKnifeModel && !safeChangeItemModel(reinterpret_cast<void*>(weapon), reinterpret_cast<void*>(item), pawn, activeWeapon)) {
			DebugConsole::rateLimited("skins.knife_model_update_failed", "[skins] knife model setter failed weapon=0x%llX",
				static_cast<unsigned long long>(weapon));
			return false;
		}
		if (!rebuildPaint(weapon, pawn, activeWeapon, skin.legacyModel)) {
			DebugConsole::rateLimited("skins.rebuild_failed", "[skins] paint rebuild failed weapon=0x%llX paint=%d",
				static_cast<unsigned long long>(weapon), skin.paintKit);
			return false;
		}

		*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bDisallowSOC) = 1;
		retainCustomMaterialAfterPrecache(item);
		if (g_invalidateItemDescription) {
			__try { g_invalidateItemDescription(reinterpret_cast<void*>(item)); }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}
		DebugConsole::logf(
			"[skins] applied weapon=0x%llX definition=%u paint=%d legacy=%d active=%d attributes=%d knife_model_update=%d stattrak_entity=%d faux_item_id=0x%llX fallback_paint=%d fallback_stattrak=%d quality=%d disallow_soc=%u restore_material=%u",
			static_cast<unsigned long long>(weapon),
			static_cast<unsigned int>(*reinterpret_cast<uint16_t*>(item + Offset::C_EconItemView::m_iItemDefinitionIndex)),
			skin.paintKit, skin.legacyModel ? 1 : 0, activeWeapon ? 1 : 0, attributesSet ? 1 : 0,
			updateKnifeModel ? 1 : 0, statTrakEntityAdded ? 1 : 0,
			static_cast<unsigned long long>(fauxItemId),
			*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackPaintKit),
			*reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackStatTrak),
			*reinterpret_cast<int*>(item + Offset::C_EconItemView::m_iEntityQuality),
			static_cast<unsigned int>(*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bDisallowSOC)),
			static_cast<unsigned int>(*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bRestoreCustomMaterialAfterPrecache)));
		g_weaponSkinRegenerationPending = true;
		DebugConsole::once("skins.mesh_group_status",
			"[skins] mesh group mask available=%d legacy=%d paint=%d",
			g_setMeshGroupMask ? 1 : 0, skin.legacyModel ? 1 : 0, skin.paintKit);
		return true;
	}

	void applyWeaponEntity(C_CSPlayerPawn* pawn, C_CSWeaponBase* entity, C_CSWeaponBase* active,
		bool activeWeaponChanged)
	{
		if (!pawn || !entity) return;
		const uintptr_t weapon = reinterpret_cast<uintptr_t>(entity);
		const CBaseHandle entityHandle = reinterpret_cast<C_BaseEntity*>(entity)->handle();
		if (!entityHandle.valid()) return;
		const uint32_t itemHandle = static_cast<uint32_t>(entityHandle.index()) |
			(static_cast<uint32_t>(entityHandle.serial_number()) << 15);
		const auto trackedHandle = g_weaponHandles.find(weapon);
		if (trackedHandle == g_weaponHandles.end() || trackedHandle->second != entityHandle) {
			if (trackedHandle != g_weaponHandles.end()) g_weaponHandles.erase(trackedHandle);
			g_weaponBaselines.erase(weapon);
			g_appliedWeaponSkins.erase(weapon);
			g_failedSkinApplyRetryAt.erase(weapon);
		}
		const uintptr_t item = weapon + Offset::C_EconEntity::m_AttributeManager +
			Offset::C_AttributeContainer::m_Item;
		const uint32_t accountId = localAccountId(pawn);
		const int definition = *reinterpret_cast<uint16_t*>(item + Offset::C_EconItemView::m_iItemDefinitionIndex);
		const bool isActive = entity == active;
		const auto baseline = g_weaponBaselines.find(weapon);
		auto applied = g_appliedWeaponSkins.find(weapon);

		if (isKnifeDefinition(definition)) {
			if (g_selectedKnife < 0 || !g_hasKnifeSkin) {
				if (baseline != g_weaponBaselines.end()) {
					__try {
						if (restoreWeapon(weapon, item, pawn, isActive, true))
							g_appliedWeaponSkins.erase(weapon);
					}
					__except (EXCEPTION_EXECUTE_HANDLER) {
						DebugConsole::rateLimited("skins.knife_restore_exception", "[skins] knife restore raised an exception");
					}
				} else {
					g_weaponHandles.erase(weapon);
				}
				return;
			}

			const KnifeEntry* knife = selectedKnife();
			if (!knife) return;
			g_weaponHandles[weapon] = entityHandle;
			captureWeaponBaseline(weapon, item);
			const uint32_t liveSubclass = *reinterpret_cast<uint32_t*>(weapon + Offset::C_BaseEntity::m_nSubclassID);
			const uint32_t targetSubclass = knifeSubclassToken(knife->definition);
			const int livePaint = *reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackPaintKit);
			const int liveStatTrak = *reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackStatTrak);
			const int liveQuality = *reinterpret_cast<int*>(item + Offset::C_EconItemView::m_iEntityQuality);
			const int expectedQuality = g_knifeSkin.statTrak >= 0 ? 3 :
				(baseline != g_weaponBaselines.end() ? baseline->second.quality : liveQuality);
			const uint64_t expectedItemId = makeFauxItemId(kKnifeFauxItemHandle);
			const bool itemStateChanged =
				*reinterpret_cast<uint64_t*>(item + Offset::C_EconItemView::m_iItemID) != expectedItemId ||
				*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bInitialized) == 0;
			const bool modelChanged = definition != knife->definition || liveSubclass != targetSubclass ||
				applied == g_appliedWeaponSkins.end() || applied->second.knifeIndex != g_selectedKnife ||
				(activeWeaponChanged && isActive);
			const bool skinChanged = modelChanged || applied == g_appliedWeaponSkins.end() ||
				applied->second.paintKit != g_knifeSkin.skin.paintKit || applied->second.seed != g_knifeSkin.seed ||
				applied->second.wear != g_knifeSkin.wear || applied->second.statTrak != g_knifeSkin.statTrak ||
				livePaint != g_knifeSkin.skin.paintKit || liveStatTrak != g_knifeSkin.statTrak ||
				liveQuality != expectedQuality || itemStateChanged ||
				applied->second.refreshRevision != g_skinRefreshRevision;
			if (!skinChanged) return;
			if (skinApplyRetryDeferred(weapon)) return;
			DebugConsole::rateLimited("skins.knife_apply_path", "[skins] knife apply path weapon=0x%llX active=%d requested_revision=%llu previous_revision=%llu",
				static_cast<unsigned long long>(weapon), isActive ? 1 : 0,
				static_cast<unsigned long long>(g_skinRefreshRevision),
				static_cast<unsigned long long>(applied == g_appliedWeaponSkins.end() ? 0 : applied->second.refreshRevision));

			initializeFunctions();
			if (skinChanged && (!g_setMeshGroupMask || !g_applyEconCustomization || !g_updateSubclass ||
				!g_regenerateWeaponSkin ||
				!g_setItemAttribute || !g_getViewModel || !g_getModelPath || !g_changeModel)) {
				deferFailedSkinApply(weapon);
				return;
			}
			if (skinChanged) {
				*reinterpret_cast<uint16_t*>(item + Offset::C_EconItemView::m_iItemDefinitionIndex) = static_cast<uint16_t>(knife->definition);
				*reinterpret_cast<int*>(item + Offset::C_EconItemView::m_iEntityQuality) = 3;
				*reinterpret_cast<uint32_t*>(weapon + Offset::C_BaseEntity::m_nSubclassID) = targetSubclass;
				g_appliedWeaponSkins.erase(weapon);
				__try {
					if (applyWeapon(weapon, item, itemHandle, g_knifeSkin, accountId, pawn, isActive, true)) {
						AppliedWeaponSkin appliedSkin{ knife->definition, g_knifeSkin.skin.paintKit,
							g_knifeSkin.seed, g_knifeSkin.statTrak, g_knifeSkin.wear, g_selectedKnife, g_skinRefreshRevision };
						g_appliedWeaponSkins[weapon] = appliedSkin;
						g_failedSkinApplyRetryAt.erase(weapon);
					} else {
						deferFailedSkinApply(weapon);
						DebugConsole::rateLimited("skins.knife_apply_failed", "[skins] knife apply failed weapon=0x%llX active=%d",
							static_cast<unsigned long long>(weapon), isActive ? 1 : 0);
					}
				}
				__except (EXCEPTION_EXECUTE_HANDLER) {
					deferFailedSkinApply(weapon);
					DebugConsole::rateLimited("skins.knife_apply_exception", "[skins] knife finish application raised an exception");
				}
			}
			return;
		}

		const auto selected = g_weaponSelection.find(definition);
		if (selected == g_weaponSelection.end()) {
			g_failedSkinApplyRetryAt.erase(weapon);
			if (baseline != g_weaponBaselines.end()) {
				__try {
					if (restoreWeapon(weapon, item, pawn, isActive))
						g_appliedWeaponSkins.erase(weapon);
				}
				__except (EXCEPTION_EXECUTE_HANDLER) {
					DebugConsole::rateLimited("skins.weapon_restore_exception", "[skins] weapon restore raised an exception");
				}
			} else {
				g_weaponHandles.erase(weapon);
			}
			g_appliedWeaponSkins.erase(weapon);
			return;
		}

		const SelectedSkin& choice = selected->second;
		g_weaponHandles[weapon] = entityHandle;
		const int livePaint = *reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackPaintKit);
		const int liveSeed = *reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackSeed);
		const float liveWear = *reinterpret_cast<float*>(weapon + Offset::C_EconEntity::m_flFallbackWear);
		const int liveStatTrak = *reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackStatTrak);
		const int liveQuality = *reinterpret_cast<int*>(item + Offset::C_EconItemView::m_iEntityQuality);
		const int expectedQuality = choice.statTrak >= 0 ? 3 :
			(baseline != g_weaponBaselines.end() ? baseline->second.quality : liveQuality);
		const uint64_t expectedItemId = makeFauxItemId(itemHandle);
		const bool itemStateChanged =
			*reinterpret_cast<uint64_t*>(item + Offset::C_EconItemView::m_iItemID) != expectedItemId ||
			*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bInitialized) == 0;
		const bool skinChanged = applied == g_appliedWeaponSkins.end() || applied->second.definition != definition ||
			applied->second.paintKit != choice.skin.paintKit || applied->second.seed != choice.seed ||
			applied->second.wear != choice.wear || applied->second.statTrak != choice.statTrak ||
			livePaint != choice.skin.paintKit || liveSeed != choice.seed || liveWear != choice.wear ||
			liveStatTrak != choice.statTrak || liveQuality != expectedQuality || itemStateChanged ||
			(activeWeaponChanged && isActive) || applied->second.refreshRevision != g_skinRefreshRevision;
		if (!skinChanged) return;
		if (skinApplyRetryDeferred(weapon)) return;
		DebugConsole::rateLimited("skins.weapon_apply_path", "[skins] weapon apply path weapon=0x%llX definition=%d active=%d requested_revision=%llu previous_revision=%llu",
			static_cast<unsigned long long>(weapon), definition, isActive ? 1 : 0,
			static_cast<unsigned long long>(g_skinRefreshRevision),
			static_cast<unsigned long long>(applied == g_appliedWeaponSkins.end() ? 0 : applied->second.refreshRevision));
		initializeFunctions();
		if (!g_setMeshGroupMask || !g_applyEconCustomization || !g_updateSubclass ||
			!g_regenerateWeaponSkin || !g_setItemAttribute) {
			deferFailedSkinApply(weapon);
			return;
		}

		g_appliedWeaponSkins.erase(weapon);
		__try {
			if (applyWeapon(weapon, item, itemHandle, choice, accountId, pawn, isActive)) {
				AppliedWeaponSkin appliedSkin{ definition, choice.skin.paintKit,
					choice.seed, choice.statTrak, choice.wear, -1, g_skinRefreshRevision };
				g_appliedWeaponSkins[weapon] = appliedSkin;
				g_failedSkinApplyRetryAt.erase(weapon);
			} else {
				deferFailedSkinApply(weapon);
				DebugConsole::rateLimited("skins.weapon_apply_failed", "[skins] weapon apply failed weapon=0x%llX definition=%d active=%d",
					static_cast<unsigned long long>(weapon), definition, isActive ? 1 : 0);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			deferFailedSkinApply(weapon);
			DebugConsole::rateLimited("skins.weapon_apply_exception", "[skins] weapon finish application raised an exception");
		}
	}

	void applyOwnedWeapons(C_CSPlayerPawn* pawn)
	{
		if (!pawn || !I::GameEntity || !I::GameEntity->Instance || !requiredSkinSchemaAvailable()) return;
		CCSPlayer_WeaponServices* services = pawn->GetWeaponServices();
		C_CSWeaponBase* active = pawn->GetActiveWeapon();
		if (services && active) {
			const CBaseHandle activeHandle = services->m_hActiveWeapon();
			if (!activeHandle.valid() || reinterpret_cast<C_BaseEntity*>(active)->handle() != activeHandle)
				active = nullptr;
		}
		const uintptr_t activeAddress = reinterpret_cast<uintptr_t>(active);
		const bool activeWeaponChanged = activeAddress != g_lastActiveWeapon;
		bool activeSeen = false;
		if (services) {
			const auto& inventory = services->m_hMyWeapons();
			if (inventory.pElements && inventory.nSize > 0 && inventory.nSize <= 64) {
				__try {
					for (uint32_t i = 0; i < inventory.nSize; ++i) {
						const CBaseHandle handle = inventory.pElements[i];
						if (!handle.valid()) continue;
						C_CSWeaponBase* weapon = I::GameEntity->Instance->Get<C_CSWeaponBase>(handle.index());
						if (!weapon || reinterpret_cast<C_BaseEntity*>(weapon)->handle() != handle) continue;
						if (weapon == active) activeSeen = true;
						applyWeaponEntity(pawn, weapon, active, activeWeaponChanged && weapon == active);
					}
				}
				__except (EXCEPTION_EXECUTE_HANDLER) {
					DebugConsole::rateLimited("skins.inventory_exception", "[skins] owned weapon iteration raised an exception");
				}
			}
		}
		if (active && !activeSeen) applyWeaponEntity(pawn, active, active, activeWeaponChanged);
		if (g_weaponSkinRegenerationPending) {
			g_weaponSkinRegenerationPending = false;
			if (g_regenerateWeaponSkins) {
				__try {
					g_regenerateWeaponSkins();
					DebugConsole::logf("[skins] global RegenerateWeaponSkins completed");
				}
				__except (EXCEPTION_EXECUTE_HANDLER) {
					DebugConsole::rateLimited("skins.regenerate_all_exception",
						"[skins] global RegenerateWeaponSkins raised an exception");
				}
			} else {
				DebugConsole::once("skins.regenerate_all_missing",
					"[skins] global RegenerateWeaponSkins signature did not resolve; per-weapon regeneration was used");
			}
		}

		bool activeLegacy = false;
		if (active) {
			const uintptr_t weapon = activeAddress;
			const uintptr_t item = weapon + Offset::C_EconEntity::m_AttributeManager + Offset::C_AttributeContainer::m_Item;
			const int definition = *reinterpret_cast<uint16_t*>(item + Offset::C_EconItemView::m_iItemDefinitionIndex);
			const int paint = *reinterpret_cast<int*>(weapon + Offset::C_EconEntity::m_nFallbackPaintKit);
			if (isKnifeDefinition(definition) && g_hasKnifeSkin) activeLegacy = g_knifeSkin.skin.legacyModel;
			else {
				const auto selection = g_weaponSelection.find(definition);
				activeLegacy = selection != g_weaponSelection.end()
					? selection->second.skin.legacyModel : legacyModelForPaintKit(paint);
			}
		}
		// The HUD weapon is a separate render-only entity and may appear after the
		// stage-7 application. Publish its desired mesh group for the render hook.
		g_activeHudMeshGroup.store(activeLegacy ? 2ull : 1ull, std::memory_order_release);

		if (activeAddress != g_lastActiveWeapon) {
			g_lastActiveWeapon = activeAddress;
			g_lastHudModelWeapon = 0;
			g_lastHudModelEntity = 0;
		}
		if (active) {
			// The HUD weapon is recreated independently of the inventory weapon.
			// Key the model restore by both identities so a replacement HUD entity
			// also gets restored after a knife -> gun switch.
			C_BaseEntity* hudWeapon = findHudModelWeapon(pawn, activeAddress);
			const uintptr_t hudAddress = reinterpret_cast<uintptr_t>(hudWeapon);
			if (hudWeapon && (activeAddress != g_lastHudModelWeapon || hudAddress != g_lastHudModelEntity) &&
				setViewModelWeaponModel(pawn, activeAddress, hudWeapon)) {
				g_lastHudModelWeapon = activeAddress;
				g_lastHudModelEntity = hudAddress;
			}
			// The global regeneration pass can replace/reset the HUD model's mesh
			// group even when the active weapon handle did not change.
			setViewModelMeshGroup(pawn, activeAddress, activeLegacy);
		} else {
			g_lastHudModelWeapon = 0;
			g_lastHudModelEntity = 0;
		}
	}

	void applyGlove(C_CSPlayerPawn* pawn)
	{
		if (!pawn || !g_hasGloveSkin || g_selectedGloveDefinition == 0) return;
		if (!requiredGloveSchemaAvailable()) return;
		const uintptr_t pawnAddress = reinterpret_cast<uintptr_t>(pawn);
		const uintptr_t item = pawnAddress + Offset::C_BasePlayerPawn::m_EconGloves;
		const uint16_t liveDefinition = *reinterpret_cast<uint16_t*>(item + Offset::C_EconItemView::m_iItemDefinitionIndex);
		const uint64_t liveId = *reinterpret_cast<uint64_t*>(item + Offset::C_EconItemView::m_iItemID);
		const uint8_t needsReapply = *reinterpret_cast<uint8_t*>(pawnAddress + Offset::C_BasePlayerPawn::m_bNeedToReApplyGloves);
		if (!needsReapply && g_lastGlovePawn == pawnAddress &&
			liveDefinition == g_selectedGloveDefinition && liveId == 0xF000000000000010ull &&
			g_lastGlovePaint == g_gloveSkin.skin.paintKit && g_lastGloveSeed == g_gloveSkin.seed &&
			g_lastGloveWear == g_gloveSkin.wear) return;

		initializeFunctions();
		if (!g_setItemAttribute) return;
		const uint32_t accountId = localAccountId(pawn);
		*reinterpret_cast<uint16_t*>(item + Offset::C_EconItemView::m_iItemDefinitionIndex) = static_cast<uint16_t>(g_selectedGloveDefinition);
		*reinterpret_cast<uint64_t*>(item + Offset::C_EconItemView::m_iItemID) = 0xF000000000000010ull;
		*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iItemIDHigh) = 0xF0000000u;
		*reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iItemIDLow) = 0x10u;
		if (accountId) *reinterpret_cast<uint32_t*>(item + Offset::C_EconItemView::m_iAccountID) = accountId;
		*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bInitialized) = 1;
		const bool paintSet = safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture prefab", static_cast<float>(g_gloveSkin.skin.paintKit));
		const bool seedSet = safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture seed", static_cast<float>(g_gloveSkin.seed));
		const bool wearSet = safeSetItemAttribute(reinterpret_cast<void*>(item), "set item texture wear", g_gloveSkin.wear);
		if (g_invalidateItemDescription) {
			__try { g_invalidateItemDescription(reinterpret_cast<void*>(item)); }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}
		*reinterpret_cast<uint8_t*>(item + Offset::C_EconItemView::m_bRestoreCustomMaterialAfterPrecache) = 1;
		*reinterpret_cast<uint8_t*>(pawnAddress + Offset::C_BasePlayerPawn::m_bNeedToReApplyGloves) = 1;
		__try { M::vfunc<void, 10>(reinterpret_cast<void*>(pawn), 1); }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		if (g_setBodygroup) {
			const int team = pawn->getTeam();
			__try {
				g_setBodygroup(reinterpret_cast<void*>(pawn), 0, 1u);
				g_setBodygroup(reinterpret_cast<void*>(pawn), team == 2 ? 0 : 1, 1u);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}
		g_lastGlovePawn = pawnAddress;
		g_lastGlovePaint = g_gloveSkin.skin.paintKit;
		g_lastGloveSeed = g_gloveSkin.seed;
		g_lastGloveWear = g_gloveSkin.wear;
		DebugConsole::once("skins.glove_apply",
			"[skins] applied glove definition=%d paint=%d attributes=%d/%d/%d",
			g_selectedGloveDefinition, g_gloveSkin.skin.paintKit,
			paintSet ? 1 : 0, seedSet ? 1 : 0, wearSet ? 1 : 0);
	}

	bool resetSkinFlagsForHandle(int entityIndex, int entitySerial,
		uint32_t attributeManagerOffset, uint32_t itemViewOffset,
		uint32_t disallowSocOffset, uint32_t restoreMaterialOffset)
	{
		if (entityIndex < 0 || !I::GameEntity || !I::GameEntity->Instance)
			return false;

		__try {
			C_CSWeaponBase* weapon = I::GameEntity->Instance->Get<C_CSWeaponBase>(entityIndex);
			if (!weapon)
				return false;

			const CBaseHandle handle = reinterpret_cast<C_BaseEntity*>(weapon)->handle();
			if (!handle.valid() || handle.index() != entityIndex || handle.serial_number() != entitySerial)
				return false;

			const uintptr_t item = reinterpret_cast<uintptr_t>(weapon) + attributeManagerOffset + itemViewOffset;
			*reinterpret_cast<uint8_t*>(item + disallowSocOffset) = 0;
			*reinterpret_cast<uint8_t*>(item + restoreMaterialOffset) = 0;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	void releaseSkinStateOnDeath()
	{
		DebugConsole::logf("[skins] death cleanup begin tracked=%llu",
			static_cast<unsigned long long>(g_weaponHandles.size()));
		// Detach state first. Cached addresses are never used after death, and a
		// failed entity lookup cannot leave the cleanup armed for every later frame.
		std::unordered_map<uintptr_t, CBaseHandle> trackedHandles;
		trackedHandles.swap(g_weaponHandles);
		g_weaponBaselines.clear();
		g_appliedWeaponSkins.clear();
		g_failedSkinApplyRetryAt.clear();
		g_lastActiveWeapon = 0;
		g_lastHudModelWeapon = 0;
		g_lastHudModelEntity = 0;
		g_lastGlovePawn = 0;
		g_lastAgentPawn = 0;

		unsigned int flagsReset = 0;
		const uint32_t attributeManagerOffset = SchemaFinder::Get("C_EconEntity->m_AttributeManager");
		const uint32_t itemViewOffset = SchemaFinder::Get("C_AttributeContainer->m_Item");
		const uint32_t disallowSocOffset = SchemaFinder::Get("C_EconItemView->m_bDisallowSOC");
		const uint32_t restoreMaterialOffset = SchemaFinder::Get("C_EconItemView->m_bRestoreCustomMaterialAfterPrecache");
		const bool schemaReady = attributeManagerOffset && itemViewOffset && disallowSocOffset && restoreMaterialOffset;
		if (schemaReady && I::GameEntity && I::GameEntity->Instance) {
			for (const auto& entry : trackedHandles) {
				const CBaseHandle handle = entry.second;
				if (!handle.valid()) continue;
				DebugConsole::logf("[skins] death cleanup checking handle index=%d serial=%d",
					handle.index(), handle.serial_number());
				if (resetSkinFlagsForHandle(handle.index(), handle.serial_number(),
					attributeManagerOffset, itemViewOffset, disallowSocOffset, restoreMaterialOffset))
					++flagsReset;
			}
		}
		DebugConsole::logf("[skins] death cleanup reset flags=%u tracked=%llu schema_ready=%d",
			flagsReset, static_cast<unsigned long long>(trackedHandles.size()), schemaReady ? 1 : 0);
	}
}

void onHudModelRendered(C_BaseEntity* model)
{
	if (!model || !g_setMeshGroupMask) return;
	__try {
		if (!isHudWeaponEntity(model)) return;
		const uint32_t sceneOffset = SchemaFinder::Get("C_BaseEntity->m_pGameSceneNode");
		if (!sceneOffset) return;
		const uintptr_t address = reinterpret_cast<uintptr_t>(model);
		const uintptr_t sceneNode = *reinterpret_cast<uintptr_t*>(address + sceneOffset);
		if (sceneNode <= 0x10000) return;
		const uint64_t meshGroup = g_activeHudMeshGroup.load(std::memory_order_acquire);
		g_setMeshGroupMask(reinterpret_cast<void*>(sceneNode), meshGroup);
		const uint64_t state = (address & 0x0000FFFFFFFFFFFFull) | (meshGroup << 60);
		if (g_lastRenderedHudMaskState.exchange(state, std::memory_order_relaxed) != state) {
			DebugConsole::logf("[skins] render HUD mesh mask target=0x%llX active_weapon=0x%llX mask=0x%llX",
				static_cast<unsigned long long>(address),
				static_cast<unsigned long long>(g_lastActiveWeapon),
				static_cast<unsigned long long>(meshGroup));
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DebugConsole::rateLimited("skins.render_hud_mesh_exception",
			"[skins] render HUD mesh mask raised an exception");
	}
}

void onItemInLoadout(std::uintptr_t itemView, std::uint32_t team, std::uint32_t loadoutSlot)
{
	const bool skinAlive = g_skinAlive.load(std::memory_order_acquire);
	const bool catalogReady = g_catalogReady.load(std::memory_order_acquire);
	if (!itemView || !skinAlive || !catalogReady) {
		DebugConsole::rateLimited("skins.loadout_view_not_ready",
			"[skins] GetItemInLoadout view skipped: view=0x%llX alive=%d catalog_ready=%d team=%u slot=%u",
			static_cast<unsigned long long>(itemView), skinAlive ? 1 : 0,
			catalogReady ? 1 : 0, team, loadoutSlot);
		return;
	}

	initializeFunctions();
	if (!g_setItemAttribute) {
		DebugConsole::rateLimited("skins.loadout_attribute_fn_missing",
			"[skins] GetItemInLoadout view skipped: SetItemAttribute function is missing");
		return;
	}

	uint16_t definitionValue = 0;
	if (!safeReadItemDefinition(itemView, &definitionValue)) {
		DebugConsole::rateLimited("skins.loadout_definition_read_failed",
			"[skins] GetItemInLoadout view skipped: item definition read failed view=0x%llX team=%u slot=%u",
			static_cast<unsigned long long>(itemView), team, loadoutSlot);
		return;
	}
	const int definition = static_cast<int>(definitionValue);
	SelectedSkin selected;
	int targetDefinition = definition;
	int quality = -1;
	bool hasSelection = false;

	{
		std::unique_lock<std::mutex> stateLock(g_stateMutex, std::try_to_lock);
		if (!stateLock.owns_lock()) {
			DebugConsole::rateLimited("skins.loadout_state_busy",
				"[skins] GetItemInLoadout view skipped: skin state busy def=%d team=%u slot=%u",
				definition, team, loadoutSlot);
			return;
		}

		if (isKnifeDefinition(definition)) {
			if (g_hasKnifeSkin) {
				const KnifeEntry* knife = selectedKnife();
				if (knife) {
					selected = g_knifeSkin;
					targetDefinition = knife->definition;
					quality = 3;
					hasSelection = true;
				}
			}
		} else {
			const auto choice = g_weaponSelection.find(definition);
			if (choice != g_weaponSelection.end()) {
				selected = choice->second;
				quality = selected.statTrak >= 0 ? 3 : 0;
				hasSelection = true;
			}
		}
	}

	if (!hasSelection) {
		DebugConsole::rateLimited("skins.loadout_no_selection",
			"[skins] GetItemInLoadout view has no selection: def=%d team=%u slot=%u",
			definition, team, loadoutSlot);
		return;
	}
	if (selected.skin.paintKit <= 0) {
		DebugConsole::rateLimited("skins.loadout_invalid_paint",
			"[skins] GetItemInLoadout view has invalid paint: def=%d paint=%d team=%u slot=%u",
			definition, selected.skin.paintKit, team, loadoutSlot);
		return;
	}
	if (!safeApplyLoadoutSkin(itemView, targetDefinition, quality, selected.skin.paintKit,
		selected.seed, selected.wear, selected.statTrak)) {
		DebugConsole::rateLimited("skins.loadout_view_apply_failed",
			"[skins] GetItemInLoadout skin apply failed: def=%d target=%d paint=%d team=%u slot=%u",
			definition, targetDefinition, selected.skin.paintKit, team, loadoutSlot);
		return;
	}

	DebugConsole::rateLimited("skins.loadout_view_override",
		"[skins] GetItemInLoadout patched view=0x%llX def=%d target=%d paint=%d team=%u slot=%u",
		static_cast<unsigned long long>(itemView), definition, targetDefinition,
		selected.skin.paintKit, team, loadoutSlot);
}

void render()
{
	beginPreviewFrame();
	std::lock_guard<std::mutex> stateLock(g_stateMutex);
	startCatalogLoad();
	ImGui::BeginChild("SkinsFeature", ImVec2(0.0f, 0.0f), false);
	ImGui::TextUnformatted("Skins");
	if (ImGui::BeginChild("SkinCategoryTabs", ImVec2(0.0f, 38.0f), false,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		const char* categoryNames[] = { "Weapons", "Knives", "Gloves", "Agents" };
		const float categoryWidth = (ImGui::GetContentRegionAvail().x
			- ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
		for (int i = 0; i < 4; ++i) {
			if (i > 0)
				ImGui::SameLine();
			const bool selected = static_cast<int>(g_category) == i;
			if (selected) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			if (ImGui::Button(categoryNames[i], ImVec2(categoryWidth, 32.0f))) {
				g_category = static_cast<Category>(i);
				activeSkinIndex() = -1;
			}
			if (selected)
				ImGui::PopStyleColor(2);
		}
		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f && ImGui::IsWindowHovered()) {
			const int nextCategory = (static_cast<int>(g_category) + (wheel < 0.0f ? 1 : 3)) % 4;
			g_category = static_cast<Category>(nextCategory);
			activeSkinIndex() = -1;
		}
	}
	ImGui::EndChild();
	ImGui::Separator();
	if (g_catalogFailed.load(std::memory_order_acquire)) {
		ImGui::TextWrapped("%s", g_error.c_str());
		ImGui::EndChild();
		return;
	}
	if (!g_catalogReady.load(std::memory_order_acquire)) {
		ImGui::TextUnformatted("Loading item catalogue...");
		ImGui::EndChild();
		return;
	}
	applyPendingSkinConfig();
	if (g_category == Category::Weapons) renderWeaponCategory();
	else if (g_category == Category::Knives) renderKnifeCategory();
	else if (g_category == Category::Gloves) renderGloveCategory();
	else renderAgentCategory();
	ImGui::EndChild();
}

nlohmann::json saveConfig()
{
	std::lock_guard<std::mutex> stateLock(g_stateMutex);
	nlohmann::json config;
	config["weapons"] = nlohmann::json::array();
	for (const auto& selection : g_weaponSelection) {
		config["weapons"].push_back({
			{"definition", selection.first},
			{"paintKit", selection.second.skin.paintKit},
			{"wear", selection.second.wear},
			{"seed", selection.second.seed},
			{"statTrak", selection.second.statTrak}
		});
	}

	config["knife"] = {
		{"enabled", g_hasKnifeSkin && selectedKnife() != nullptr},
		{"definition", selectedKnife() ? selectedKnife()->definition : 0},
		{"paintKit", g_knifeSkin.skin.paintKit},
		{"wear", g_knifeSkin.wear},
		{"seed", g_knifeSkin.seed},
		{"statTrak", g_knifeSkin.statTrak}
	};
	config["gloves"] = {
		{"enabled", g_hasGloveSkin && g_selectedGloveDefinition != 0},
		{"definition", g_selectedGloveDefinition},
		{"paintKit", g_gloveSkin.skin.paintKit},
		{"wear", g_gloveSkin.wear},
		{"seed", g_gloveSkin.seed}
	};
	config["agents"] = {{"ct", g_agentCtModel}, {"t", g_agentTModel}};
	return config;
}

void loadConfig(const nlohmann::json& config)
{
	if (!config.is_object()) return;
	std::lock_guard<std::mutex> stateLock(g_stateMutex);
	g_pendingSkinConfig = config;
	g_hasPendingSkinConfig = true;
	startCatalogLoad();
	applyPendingSkinConfig();
}

void onFrameStage(int stage, C_CSPlayerPawn* localPawn)
{
	std::lock_guard<std::mutex> stateLock(g_stateMutex);
	if (stage != 7) return;
	DebugConsole::once("skins.frame_stage7", "[skins] stage 7 reached; local pawn=%p", localPawn);
	if (!localPawn || localPawn->getHealth() <= 0) {
		g_skinAlive.store(false, std::memory_order_release);
		g_postRespawnSkinReapplyPending = false;
		if (!g_skinDeathHandled) {
			g_skinDeathHandled = true;
			DebugConsole::logf("[skins] death transition; releasing skin state at stage 7");
			releaseSkinStateOnDeath();
		}
		return;
	}
	g_skinAlive.store(true, std::memory_order_release);
	if (g_skinDeathHandled) g_postRespawnSkinReapplyPending = true;
	g_skinDeathHandled = false;
	startCatalogLoad();
	if (!g_catalogReady.load(std::memory_order_acquire)) {
		DebugConsole::once("skins.catalog_wait", "[skins] waiting for embedded skin catalogs");
		return;
	}
	applyPendingSkinConfig();
	applyGlove(localPawn);
	const int team = localPawn->getTeam();
	std::string model = team == 3 ? g_agentCtModel : g_agentTModel;
	if (model.empty() && g_lastAgentPawn == reinterpret_cast<uintptr_t>(localPawn) && !g_lastAgentModel.empty())
		model = defaultAgentModel(team);
	if (!model.empty() && (g_lastAgentPawn != reinterpret_cast<uintptr_t>(localPawn) ||
		g_lastAgentModel != model)) {
		initializeFunctions();
		if (safeChangeModel(localPawn, model.c_str())) {
			g_lastAgentPawn = reinterpret_cast<uintptr_t>(localPawn);
			g_lastAgentModel = model;
		}
	}
	// Use stage-7 pre-original ordering so the model/material changes
	// are present when the engine begins its stage callback.
	applyOwnedWeapons(localPawn);
}

void onFrameStagePost(int stage, C_CSPlayerPawn* localPawn)
{
	if (stage != 7 || !localPawn || localPawn->getHealth() <= 0) return;
	std::lock_guard<std::mutex> stateLock(g_stateMutex);
	if (!g_catalogReady.load(std::memory_order_acquire)) return;
	DebugConsole::rateLimited("skins.post_stage_apply_tick",
		"[skins] post-stage apply tick pawn=%p health=%d revision=%llu active_weapon=%p",
		localPawn, localPawn->getHealth(), static_cast<unsigned long long>(g_skinRefreshRevision),
		localPawn->GetActiveWeapon());

	// A pre-stage apply can look current while the engine's callback replaces the
	// knife model. Force one full post-network rebuild after respawn so the model
	// and paint are applied to the final live weapon state.
	if (g_postRespawnSkinReapplyPending) {
		g_appliedWeaponSkins.clear();
		g_failedSkinApplyRetryAt.clear();
		DebugConsole::logf("[skins] post-network respawn rebuild pawn=%p active_weapon=%p",
			localPawn, localPawn->GetActiveWeapon());
		applyOwnedWeapons(localPawn);
		g_postRespawnSkinReapplyPending = false;
		return;
	}
	// Otherwise, recheck after the engine's stage-7 callback in case it rewrote
	// fallback fields or rebuilt the active viewmodel after the pre-stage pass.
	applyOwnedWeapons(localPawn);
}
}
