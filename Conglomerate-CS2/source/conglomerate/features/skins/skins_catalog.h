#pragma once

#include <string>
#include <vector>

namespace SkinsCatalog
{
	struct SkinEntry
	{
		int paintKit = 0;
		int weaponId = 0;
		std::string name;
		std::string searchName;
		std::string weaponName;
		std::string rarityName;
		std::string image;
		float minWear = 0.0f;
		float maxWear = 1.0f;
		int rarity = 1;
		bool knife = false;
		bool glove = false;
		bool legacyModel = false;
	};

	struct AgentEntry
	{
		int definition = 0;
		std::string name;
		std::string searchName;
		std::string image;
		std::string model;
		std::string team;
		bool counterTerrorist = false;
	};

	struct Data
	{
		std::vector<SkinEntry> skins;
		std::vector<AgentEntry> agents;
		std::string error;
	};

	Data load();
}
