#include "skins_catalog.h"

#include "../../../../external/json/json.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

extern HMODULE g_moduleInstance;

namespace SkinsCatalog
{
namespace
{
	std::string resourceText(int id)
	{
		HMODULE module = g_moduleInstance;
		if (!module)
			GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(&resourceText), &module);
		if (!module) module = GetModuleHandleA(nullptr);
		HRSRC resource = FindResourceA(module, MAKEINTRESOURCEA(id), RT_RCDATA);
		if (!resource) return {};
		HGLOBAL loaded = LoadResource(module, resource);
		if (!loaded) return {};
		const DWORD size = SizeofResource(module, resource);
		const auto* bytes = static_cast<const char*>(LockResource(loaded));
		if (!bytes || !size) return {};
		return std::string(bytes, bytes + size);
	}

	std::string stringValue(const nlohmann::json& value, const char* key)
	{
		if (!value.is_object() || !value.contains(key)) return {};
		const auto& field = value[key];
		return field.is_string() ? field.get<std::string>() : std::string{};
	}

	int intValue(const nlohmann::json& value, const char* key, int fallback = 0)
	{
		if (!value.is_object() || !value.contains(key)) return fallback;
		const auto& field = value[key];
		if (field.is_number_integer()) return field.get<int>();
		if (field.is_string()) {
			try { return std::stoi(field.get<std::string>()); } catch (...) {}
		}
		return fallback;
	}

	float floatValue(const nlohmann::json& value, const char* key, float fallback)
	{
		if (!value.is_object() || !value.contains(key)) return fallback;
		const auto& field = value[key];
		if (field.is_number()) return field.get<float>();
		return fallback;
	}

	std::string normalizeText(const std::string& text)
	{
		std::string normalized = text;
		std::transform(normalized.begin(), normalized.end(), normalized.begin(),
			[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return normalized;
	}
}

Data load()
{
	Data data;
	const std::string skinsText = resourceText(102);
	const std::string agentsText = resourceText(103);
	if (skinsText.empty() || agentsText.empty()) {
		data.error = "Embedded Raven catalog resources could not be read.";
		return data;
	}

	auto skinsJson = nlohmann::json::parse(skinsText, nullptr, false);
	auto agentsJson = nlohmann::json::parse(agentsText, nullptr, false);
	if (skinsJson.is_discarded() || !skinsJson.is_array() ||
		agentsJson.is_discarded() || !agentsJson.is_array()) {
		data.error = "Embedded catalog JSON is invalid.";
		return data;
	}

	data.skins.reserve(skinsJson.size());
	for (const auto& item : skinsJson) {
		if (!item.is_object()) continue;
		SkinEntry entry;
		entry.paintKit = intValue(item, "paint_index");
		entry.name = stringValue(item, "name");
		entry.searchName = normalizeText(entry.name);
		entry.image = stringValue(item, "image");
		entry.minWear = floatValue(item, "min_float", 0.0f);
		entry.maxWear = floatValue(item, "max_float", 1.0f);
		entry.legacyModel = item.value("legacy_model", false);
		entry.weaponName = item.contains("weapon") ? stringValue(item["weapon"], "name") : std::string{};
		entry.rarityName = item.contains("rarity") ? stringValue(item["rarity"], "name") : std::string{};
		entry.weaponId = item.contains("weapon") ? intValue(item["weapon"], "weapon_id") : 0;
		entry.glove = entry.weaponId == 4725 ||
			(entry.weaponId >= 5027 && entry.weaponId <= 5035);
		entry.knife = !entry.glove && entry.weaponId >= 500 && entry.weaponId <= 526;
		if (entry.paintKit <= 0 || entry.name.empty()) continue;
		if (item.contains("rarity") && item["rarity"].is_object()) {
			const std::string rarity = stringValue(item["rarity"], "id");
			entry.rarity = rarity.find("contraband") != std::string::npos ? 7 :
				rarity.find("ancient") != std::string::npos ? 6 :
				rarity.find("legendary") != std::string::npos ? 5 :
				rarity.find("mythical") != std::string::npos ? 4 :
				rarity.find("rare") != std::string::npos ? 3 :
				rarity.find("uncommon") != std::string::npos ? 2 : 1;
		}
		data.skins.emplace_back(std::move(entry));
	}

	data.agents.reserve(agentsJson.size());
	for (const auto& item : agentsJson) {
		if (!item.is_object()) continue;
		AgentEntry entry;
		entry.definition = intValue(item, "def_index");
		entry.name = stringValue(item, "name");
		entry.searchName = normalizeText(entry.name);
		entry.image = stringValue(item, "image");
		entry.model = stringValue(item, "model_player");
		if (item.contains("team")) entry.team = stringValue(item["team"], "name");
		entry.counterTerrorist = entry.team.find("Counter") != std::string::npos || entry.team == "CT";
		if (entry.definition > 0 && !entry.model.empty()) data.agents.emplace_back(std::move(entry));
	}
	return data;
}
}
