#include "config.h"


namespace Config {
	bool esp = false;
	bool glow = false;
	bool showHealth = false;
	bool teamCheck = false;
	bool espFill = false;
	bool showNameTags = false;
	bool flags = false;
	bool skeleton = false;
	ImVec4 skeletonColor = ImVec4(1, 1, 1, 1);
	float skeletonThickness = 1.0f;

	bool Night = false;

	bool enemyChamsInvisible = false;
	bool enemyChams = false;
	bool teamChams = false;
	bool teamChamsInvisible = false;
	int chamsMaterial = 0;


	bool armChams = false;
	bool viewmodelChams = false;
	int armChamsMaterial = 0;
	int viewmodelChamsMaterial = 0;
	ImVec4 colViewmodelChams = ImVec4(1, 0, 0, 1);
	ImVec4 colArmChams = ImVec4(1, 0, 0, 1);

	ImVec4 colVisualChams = ImVec4(1, 0, 0, 1);
	ImVec4 colVisualChamsIgnoreZ = ImVec4(1, 0, 0, 1);
	ImVec4 teamcolVisualChamsIgnoreZ = ImVec4(1, 0, 0, 1);
	ImVec4 teamcolVisualChams = ImVec4(1, 0, 0, 1);

	float espThickness = 1.0f;
	float espFillOpacity = 0.5f;
	ImVec4 espColor = ImVec4(1, 0, 0, 1);
	ImVec4 espColorT = ImVec4(0.92f, 0.55f, 0.15f, 1.0f);  
	ImVec4 espColorCT = ImVec4(0.25f, 0.55f, 0.95f, 1.0f); 

	bool fovEnabled = false;
	float fov = 90.0f;
	bool watermark = true;
	bool spectatorList = false;

	bool antiflash = false;
	bool noSmoke = false;

	ImVec4 NightColor = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);

	bool aimbot = 0;
	float aimbot_fov = 0;
	bool team_check = false;
	bool fov_circle = 0;
	ImVec4 fovCircleColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}
