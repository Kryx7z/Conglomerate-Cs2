#pragma once
#include "../../../external/imgui/imgui.h"


namespace Config {
	extern bool esp;
	extern bool showHealth;
	extern bool teamCheck;
	extern bool espFill;
	extern float espThickness;
	extern float espFillOpacity;
	extern ImVec4 espColor;
	extern ImVec4 espColorT;
	extern ImVec4 espColorCT;
	extern bool showNameTags;
	extern bool flags;
	extern bool skeleton;
	extern ImVec4 skeletonColor;
	extern float skeletonThickness;

	extern bool Night;
	extern ImVec4 NightColor;
	extern ImVec4 NightSkyColor;
	extern bool weatherRainEnabled;
	extern float weatherRainIntensity;
	extern ImVec4 weatherRainColor;
	extern bool weatherWindEnabled;
	extern float weatherWindStrength;
	extern float weatherWindDirection;
	extern float weatherWindTurbulence;
	extern bool weatherFogEnabled;
	extern float weatherFogDensity;
	extern float weatherFogAnisotropy;
	extern float weatherFogDistance;
	extern ImVec4 weatherFogColor;
	extern bool bunnyHop;

	extern bool enemyChamsInvisible;
	extern bool enemyChams;
	extern bool teamChams;
	extern bool teamChamsInvisible;
	extern int chamsMaterial;

	extern ImVec4 colVisualChams;
	extern ImVec4 colVisualChamsIgnoreZ;
	extern ImVec4 teamcolVisualChamsIgnoreZ;
	extern ImVec4 teamcolVisualChams;

	extern bool armChams;
	extern bool viewmodelChams;
	extern int armChamsMaterial;
	extern int viewmodelChamsMaterial;
	extern ImVec4 colViewmodelChams;
	extern ImVec4 colArmChams;

	extern bool fovEnabled;
	extern float fov;
	extern bool watermark;
	extern bool spectatorList;
	extern bool awpCrosshair;

	extern bool antiflash;
	extern bool noSmoke;

	extern bool aimbot;
	extern bool aimbotVisibleCheck;
	extern bool recoilControl;
	extern float recoilControlStrength;
	extern float recoilControlSmoothing;
	extern bool triggerbot;
	extern bool triggerbot_team_check;
	extern float aimbot_fov;
	extern bool team_check;
	extern bool fov_circle;
	extern ImVec4 fovCircleColor;
}
