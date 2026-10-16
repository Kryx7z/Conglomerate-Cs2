#pragma once

#include <cstdint>
#include "../../../../external/json/json.hpp"

class C_CSPlayerPawn;
class C_BaseEntity;

namespace Skins {
	void render();
	nlohmann::json saveConfig();
	void loadConfig(const nlohmann::json& config);
	void onFrameStage(int stage, C_CSPlayerPawn* localPawn);
	void onFrameStagePost(int stage, C_CSPlayerPawn* localPawn);
	void onHudModelRendered(C_BaseEntity* model);
	void onItemInLoadout(std::uintptr_t itemView, std::uint32_t team, std::uint32_t loadoutSlot);
}
